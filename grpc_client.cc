#include <grpc++/grpc++.h> 
#include <grpcpp/impl/codegen/byte_buffer.h>
#include <grpcpp/impl/codegen/client_callback.h>

#include "src/core/lib/json/json.h"
#include "src/core/lib/surface/channel.h"

#include "grpc_client.h"

#include <unordered_map>


//
// Status
//

Status::Status() : code_(grpc::OK) {}

Status FromGrpcStatus(const grpc::Status& s) {
  Status r;
  r.code_ = s.error_code();
  r.message_ = s.error_message();
  r.details_ = s.error_details();
  return r;
}

bool Status::Ok() const { return code_ == grpc::OK; }

//
// Context
//
struct ClientContextImpl: ClientContext {
  std::string Peer() override {
    return peer_;
  }

  void SetTimeoutMicros(int to) override {
    if (to > 0) {
      auto gprto =
          gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                     gpr_time_from_micros(to, GPR_TIMESPAN));
      ctx_->set_deadline(gprto);
    }
  }

  void AddMetadata(const std::string& k, const std::string& v) override {
    ctx_->AddMetadata(k, v);
  }

  void capturePeer() {
    peer_ = ctx_->peer();
  }

  void TryCancel() override {
    ctx_->TryCancel();
  }

  static inline ClientContextImpl* from(ClientContext* c) {
    return static_cast<ClientContextImpl*>(c);
  }

  // Cancel calls when this object goes out of scope.
  ~ClientContextImpl() { ctx_->TryCancel(); }

  std::unique_ptr<grpc::ClientContext> ctx_;
  std::string peer_;
};

std::shared_ptr<ClientContext> ClientContext::New() {
  auto impl = new ClientContextImpl;
  impl->ctx_.reset(new grpc::ClientContext());
  // All our usage of the API suggests we would never be intrested in
  // pre-emptively sending metadata (and waiting for a response) in advance of
  // the first message.
  impl->ctx_->set_initial_metadata_corked(true);
  return std::shared_ptr<ClientContext>(impl);
}

//
// ChannelArguments
//
struct ChannelArgumentsImpl : ChannelArguments {
  void SetMaxReceiveMessageSize(int size) override { args_->SetMaxReceiveMessageSize(size); }
  void SetMaxSendMessageSize(int size) override { args_->SetMaxSendMessageSize(size); }
  void SetLoadBalancingPolicyName(const std::string& lb) override { args_->SetLoadBalancingPolicyName(lb); }  
  void SetServiceConfigJSON(const std::string& j) override { args_->SetServiceConfigJSON(j); }
  
  static inline ChannelArgumentsImpl* from(ChannelArguments* c) {
    return static_cast<ChannelArgumentsImpl*>(c);
  }

  std::string DebugNormalized() override {
    grpc_channel_args args;
    args_->SetChannelArgs(&args);
    auto normal = grpc_channel_args_normalize(&args);
    auto ret = grpc_channel_args_string(normal);
    grpc_channel_args_destroy(normal);
    return ret;
  }

  std::unique_ptr<grpc::ChannelArguments> args_;
};

std::shared_ptr<ChannelArguments> ChannelArguments::New() {
  auto r = new ChannelArgumentsImpl();
  r->args_.reset(new grpc::ChannelArguments);
  return std::shared_ptr<ChannelArguments>(r);
}

//
// Channel
//
struct ChannelImpl : Channel {
  std::shared_ptr<grpc::Channel> channel_;
  grpc_channel *core_channel_; // not owned

  std::shared_ptr<ChannelArguments> args_;
  std::string name_;
  std::string target_;

  void
  UnaryCall(const std::string &method,
      std::shared_ptr<ClientContext> ctx,
      std::shared_ptr<Serializer> req,
      UnaryResultEvents *ev) override;

  std::unique_ptr<StreamReader> ServerStreamingCall(
      const std::string &method,
      std::shared_ptr<Serializer> req,
      std::shared_ptr<ClientContext> ctx) override;

  grpc_core::Json DebugJson() {
    grpc_core::Json::Object ret;
    ret["name"] = name_;
    ret["target"] = target_;
    ret["normalized_args"] = args_->DebugNormalized();
    ret["channelz"] = grpc_channel_get_channelz_node(core_channel_)->RenderJson();
    ret["state"] = grpc_core::ConnectivityStateName(channel_->GetState(false /* try to connect */));
    ret["lb_policy_name"] = channel_->GetLoadBalancingPolicyName();
    ret["service_config_json"] = channel_->GetServiceConfigJSON();
    return ret;
  }

  std::string Debug() override { return DebugJson().Dump(2); }
};

void
ChannelImpl::UnaryCall(
    const std::string& method,
    std::shared_ptr<ClientContext> ctx,
    std::shared_ptr<Serializer> req,
    UnaryResultEvents *ev) {
  // TODO consider using the ClientUnaryReactor API instead, which would give a
  // hook for initial metadata being ready.
  auto meth = grpc::internal::RpcMethod(method.c_str(),
                                        grpc::internal::RpcMethod::NORMAL_RPC);
  ::grpc::internal::CallbackUnaryCall<
      Serializer, UnaryResultEvents,
      Serializer, Deserializer>(
      channel_.get(), meth, ClientContextImpl::from(ctx.get())->ctx_.get(), req.get(), ev, [ctx, req, ev](grpc::Status s) {
        // req.reset(nullptr);
        ClientContextImpl::from(ctx.get())->capturePeer();
        ev->Done(FromGrpcStatus(s));
      });
}

template <class T>
class ClientReadReactor : public ::grpc::experimental::ClientReadReactor<T>, public StreamReader {
  public:
ClientReadReactor() : done_(false) {}
  void OnDone(const ::grpc::Status& s) override {
    done_ = true;
    status_ = FromGrpcStatus(s);
    ClientContextImpl::from(ctx_.get())->capturePeer();
    ev_->Done(status_, false);
  }

  void OnReadInitialMetadataDone(bool ok) override {
    req_.reset(); // Relase our reference to the request buffer.
    ClientContextImpl::from(ctx_.get())->capturePeer();
  }

  virtual void OnReadDone(bool ok) override {
    if (ok) {
      this->AddHold(); // Stops OnDone from being invoked until RemoveHold()
      ev_->Done(status_, true);
      ev_ = nullptr;
    }
  }
  
  virtual void Next(StreamReadEvents *ev) override {
    if (done_) {
      // Only expected if someone invokes Next() after it returns false;
      ev->Done(status_, false);
    }
    ev_ = ev;
    this->StartRead(ev);
    this->RemoveHold();
  }

  bool done_;
  Status status_;
  std::shared_ptr<ClientContext> ctx_;
  StreamReadEvents* ev_;  
  std::shared_ptr<Serializer> req_;
};

std::unique_ptr<StreamReader> ChannelImpl::ServerStreamingCall(
    const std::string& method,
    std::shared_ptr<Serializer> req,
    std::shared_ptr<ClientContext> ctx) {
  auto meth = grpc::internal::RpcMethod(method.c_str(),
                                        grpc::internal::RpcMethod::SERVER_STREAMING);
  auto reactor = new ClientReadReactor<Deserializer>();
  reactor->ctx_ = ctx;
  reactor->req_ = req;
  ::grpc::internal::ClientCallbackReaderFactory<Deserializer>::Create(
    channel_.get(),
    meth, 
    ClientContextImpl::from(ctx.get())->ctx_.get(),
    req.get(),
    reactor
    );
  reactor->AddHold();
  reactor->StartCall();
  return std::unique_ptr<StreamReader>(reactor);
}


//
// ChannelStore
//

class ChannelStore {
public:
  static ChannelStore *Singleton;

  std::shared_ptr<ChannelImpl> GetChannel(const std::string &name,
                                          const std::string &target,
                                          std::shared_ptr<ChannelArguments> args) {
    std::lock_guard<std::mutex> guard(mu_);
    auto it = map_.find(name);
    if (it != map_.end()) {
      return it->second;
    }
    // auto cch = grpc::InsecureChannelCredentials()->CreateChannelImpl(target,
    // args);
    // We do a more complicated form of the above so we can get access to the
    // core channel pointer, which in turn can be used with channelz apis.
    grpc_channel_args channel_args;
    ChannelArgumentsImpl::from(args.get())->args_->SetChannelArgs(&channel_args);

    auto record = std::make_shared<ChannelImpl>();
    record->args_ = args;
    record->name_ = name;
    record->target_ = target;
    record->core_channel_ =
        grpc_insecure_channel_create(target.c_str(), &channel_args, nullptr);
    std::vector<
        std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>>
        interceptor_creators;
    record->channel_ = ::grpc::CreateChannelInternal(
        "", record->core_channel_, std::move(interceptor_creators));
    map_[name] = record;
    return record;
  }

  std::string Debug() {
    grpc_core::Json::Object ret;
    std::lock_guard<std::mutex> guard(mu_);
    for (auto it = map_.begin(); it != map_.end(); it++) {
      ret[it->first] = it->second->DebugJson();
    }
    return grpc_core::Json(ret).Dump(2);
  }

private:
  std::mutex mu_;
  std::unordered_map<std::string, std::shared_ptr<ChannelImpl>> map_;
};

ChannelStore *ChannelStore::Singleton = nullptr;

std::shared_ptr<Channel> GetChannel(const std::string &name,
                                    const std::string &target,
                                    std::shared_ptr<ChannelArguments> args) {
  return ChannelStore::Singleton->GetChannel(name, target, std::move(args));
}

//
// SerDe
//

struct ResponseImpl : Response {
  Status Slices(SliceList *list) override {
    std::vector<grpc::Slice> slices;
    auto status = bb_.Dump(&slices);
    if (!status.ok()) {
      return FromGrpcStatus(status);
    }
    for (size_t i=0; i<slices.size(); i++ ) {
      list->push_back(
          std::make_pair<const uint8_t *, size_t>(slices[i].begin(), slices[i].size()));
    }
    return FromGrpcStatus(grpc::Status::OK);
  }
  grpc::ByteBuffer bb_;
};

//
// Housekeeping.
//

void Init() {
  grpc_init();
  //gpr_set_log_verbosity(GPR_LOG_SEVERITY_DEBUG);
  ChannelStore::Singleton = new ChannelStore();
}

std::string Version() {
  return grpc::Version();
}

std::string DebugAllChannels() {
  return ChannelStore::Singleton->Debug();
}

// https://stackoverflow.com/questions/25594644/warning-specialization-of-template-in-different-namespace
namespace grpc {
  // https://grpc.github.io/grpc/cpp/classgrpc_1_1_serialization_traits.html
  template <> class SerializationTraits<::Serializer, void> {
  public:
    static grpc::Status Serialize(const ::Serializer &source,
                                  ByteBuffer *buffer, bool *own_buffer) {
      const void *c;
      size_t l;
      source.FillRequest(&c, &l);
      *own_buffer = true;
      Slice slice(c, l, grpc::Slice::STATIC_SLICE);
      ByteBuffer tmp(&slice, 1);
      buffer->Swap(&tmp);
      return Status::OK;
    }
  };

  // https://grpc.github.io/grpc/cpp/classgrpc_1_1_serialization_traits.html
  template <> class SerializationTraits<::Deserializer, void> {
  public:
    static grpc::Status Deserialize(ByteBuffer *byte_buffer,
                                    ::Deserializer *dest) {
      auto r = new ResponseImpl();
      r->bb_.Swap(byte_buffer);
      dest->ResponseReady(std::move(std::unique_ptr<Response>(r)));
      return Status::OK;
    }
  };
}
