#include <iostream>

#include "hphp/runtime/base/array-init.h"
#include "hphp/runtime/base/array-iterator.h"
#include "hphp/runtime/base/builtin-functions.h"
#include "hphp/runtime/ext/asio/asio-external-thread-event.h"
#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/vm/native-data.h"

#include <grpc_client.h>

namespace HPHP {

#define NATIVE_DATA_CLASS(cls, hackcls, ptr, assign)                           \
  struct cls {                                                                 \
    static Class *s_class;                                                     \
    static const StaticString s_hackClassName;                                 \
    static const StaticString s_cppClassName;                                  \
    static Class *getClass() {                                                 \
      if (s_class == nullptr) {                                                \
        s_class = Unit::lookupClass(s_hackClassName.get());                    \
        assertx(s_class);                                                      \
      }                                                                        \
      return s_class;                                                          \
    }                                                                          \
    static Object newInstance(ptr data) {                                      \
      Object obj{getClass()};                                                  \
      auto *d = Native::data<cls>(obj);                                        \
      d->data_ = assign;                                                       \
      return obj;                                                              \
    }                                                                          \
    cls(){};                                                                   \
    ptr data_;                                                                 \
    cls(const cls &) = delete;                                                 \
    cls &operator=(const cls &) = delete;                                      \
  };                                                                           \
  Class *cls::s_class = nullptr;                                               \
  const StaticString cls::s_hackClassName(#hackcls);                           \
  const StaticString cls::s_cppClassName(#cls);

#define NATIVE_GET_METHOD(rettype, cls, meth, expr)                            \
  static rettype HHVM_METHOD(cls, meth) {                                      \
    auto *d = Native::data<cls>(this_);                                        \
    return expr;                                                               \
  }

#define NATIVE_SET_METHOD(cls, meth, expr, ...)                                \
  static void HHVM_METHOD(cls, meth, __VA_ARGS__) {                            \
    auto *d = Native::data<cls>(this_);                                        \
    expr;                                                                      \
  }

//
// Status
//
NATIVE_DATA_CLASS(GrpcStatus, GrpcNative\\Status, Status, data);
NATIVE_GET_METHOD(int, GrpcStatus, Code, d->data_.code_);
NATIVE_GET_METHOD(String, GrpcStatus, Message, d->data_.message_);
NATIVE_GET_METHOD(String, GrpcStatus, Details, d->data_.details_);

//
// ClientContext
//
NATIVE_DATA_CLASS(GrpcClientContext, GrpcNative\\ClientContext,
                  std::shared_ptr<ClientContext>, std::move(data));
Object HHVM_STATIC_METHOD(GrpcClientContext, Create) {
  return GrpcClientContext::newInstance(std::move(ClientContext::New()));
}
NATIVE_GET_METHOD(String, GrpcClientContext, Peer, d->data_->Peer());
NATIVE_SET_METHOD(GrpcClientContext, SetTimeoutMicros,
                  d->data_->SetTimeoutMicros(p), int p);
static void HHVM_METHOD(GrpcClientContext, TryCancel) {
  auto *d = Native::data<GrpcClientContext>(this_);
  d->data_->TryCancel();
}
static void HHVM_METHOD(GrpcClientContext, AddMetadata, const Array &md) {
  auto *d = Native::data<GrpcClientContext>(this_);
  for (auto it = md.begin(); !it.end(); it.next()) {
    auto k = it.first().toString().toCppString();
    for (auto vit = it.second().toArray().begin(); !vit.end(); vit.next()) {
      d->data_->AddMetadata(k, vit.second().toString().toCppString());
    }
  }
}

//
// Channel
//
NATIVE_DATA_CLASS(GrpcChannel, GrpcNative\\Channel, std::shared_ptr<Channel>,
                  std::move(data));
NATIVE_GET_METHOD(String, GrpcChannel, Debug, d->data_->Debug());

//
// Call Plubming.
//
struct GrpcCallResultData : Deserializer {
  // This function *must* be called on a HHVM request thread, not an
  // ASIO/callback thread. It should be called before any call to
  // UnserializeStatus.
  String UnserializeResponse() {
    if (!gresp_) {
      return String("");
    }
    SliceList slices;
    auto status = gresp_->Slices(&slices);
    if (!status.Ok()) {
      gresp_.reset(); // Drop our reference to the response buffer.
      // We may already be in an error state, in which case we prefer the first
      // error message.
      if (status_.Ok()) {
        status_ = status;
      }
      return String("");
    }
    size_t total = 0;
    for (auto s : slices) {
      total += s.second;
    }
    auto buf = StringData::Make(total);
    auto pos = buf->mutableData();
    for (auto s : slices) {
      memcpy(pos, s.first, s.second);
      pos += s.second;
    }
    buf->setSize(total);
    gresp_.reset(); // Drop our reference to the response buffer.
    return String(buf);
  }

  // This function *must* be called on a HHVM request thread, not an
  // ASIO/callback thread. It should be called before any call to
  // UnserializeStatus.
  Object UnserializeStatus() { return GrpcStatus::newInstance(status_); }

  void ResponseReady(std::unique_ptr<Response> r) override {
    gresp_ = std::move(r);
  }

  Status status_;
  std::unique_ptr<Response> gresp_;
};

// GrpcSerializer holds a reference to the request buffer (HHVM String) and
// provides it to grpc. It should remain alive until initial metadata is
// received or the grpc call is done.
struct GrpcSerializer : Serializer {
  GrpcSerializer(const String &s) : req_(s) {}
  void FillRequest(const void **c, size_t *l) const override {
    *c = req_.data();
    *l = req_.size();
  }

private:
  String req_;
};

//
// UnaryCall
//
struct GrpcEvent final : AsioExternalThreadEvent, UnaryResultEvents {
public:
  GrpcEvent(const String &req)
      : data_(std::make_unique<GrpcCallResultData>()), req_(req) {}

  void Done(const Status &s) override {
    data_->status_ = s;
    markAsFinished();
  }

  void ResponseReady(std::unique_ptr<Response> r) override {
    data_->ResponseReady(std::move(r));
  }

private:
  std::unique_ptr<GrpcCallResultData> data_;
  String req_;

protected:
  // Invoked by the ASIO Framework after we have markAsFinished(); this is
  // where we return data to PHP.
  void unserialize(TypedValue &result) override final {
    auto resp = data_->UnserializeResponse();
    auto resTuple = make_varray(resp, data_->UnserializeStatus());
    tvCopy(make_array_like_tv(resTuple.detach()), result);
  }
};

static Object HHVM_METHOD(GrpcChannel, UnaryCall, const Object &ctx,
                          const String &method, const String &req) {
  auto event = new GrpcEvent(req);
  auto *d = Native::data<GrpcChannel>(this_);
  auto *dctx = Native::data<GrpcClientContext>(ctx);
  auto ser = std::shared_ptr<Serializer>(new GrpcSerializer(req));
  d->data_->UnaryCall(method.toCppString(), dctx->data_, ser, event);
  return Object{event->getWaitHandle()};
}

//
// Streaming Call
//
struct GrpcStreamReaderData {
  String resp_;
  std::unique_ptr<StreamReader> reader_;
  std::unique_ptr<GrpcCallResultData> result_;
};

struct GrpcReadStreamEvent final : AsioExternalThreadEvent, StreamReadEvents {
  void ResponseReady(std::unique_ptr<Response> r) override {
    data_->result_->ResponseReady(std::move(r));
  }

  void Done(const Status &status, bool success) override {
    success_ = success;
    data_->result_->status_ = status;
    markAsFinished();
  }

  std::shared_ptr<GrpcStreamReaderData> data_;
  bool success_;

protected:
  // Invoked by the ASIO Framework after we have markAsFinished(); this is
  // where we return data to PHP.
  void unserialize(TypedValue &result) override final {
    if (success_) {
      data_->resp_ = data_->result_->UnserializeResponse();
      if (!data_->result_->status_.Ok()) {
        // Deserializastion failed.
        success_ = false;
      }
    }
    tvCopy(make_tv<KindOfBoolean>(success_), result);
  }
};

NATIVE_DATA_CLASS(GrpcStreamReader, GrpcNative\\StreamReader,
                  std::shared_ptr<GrpcStreamReaderData>, std::move(data));
NATIVE_GET_METHOD(Object, GrpcStreamReader, Status,
                  d->data_->result_->UnserializeStatus());
NATIVE_GET_METHOD(String, GrpcStreamReader, Response, d->data_->resp_);

static Object HHVM_METHOD(GrpcStreamReader, Next) {
  auto *d = Native::data<GrpcStreamReader>(this_);
  auto nextResult = new GrpcCallResultData();
  d->data_->result_.reset(nextResult);

  auto event = new GrpcReadStreamEvent();
  event->data_ = d->data_; // If the stream reader goes out of scope, don't
                           // destroy the data until the event is finished.

  // TODO cy you need to probably implement trycancel here and for unary...
  // what if deadline is forever and server never returns and all this stuff
  // goes out of scope? Hm, probably implement it on the client context
  // destructor.

  d->data_->reader_->Next(event);
  return Object{event->getWaitHandle()};
}

static Object HHVM_METHOD(GrpcChannel, ServerStreamingCall, const Object &ctx,
                          const String &method, const String &req) {
  auto ser = std::shared_ptr<Serializer>(new GrpcSerializer(req));
  auto *d = Native::data<GrpcChannel>(this_);
  auto *dctx = Native::data<GrpcClientContext>(ctx);
  auto readerData = std::make_shared<GrpcStreamReaderData>();
  readerData->reader_ = std::move(
      d->data_->ServerStreamingCall(method.toCppString(), ser, dctx->data_));
  return GrpcStreamReader::newInstance(readerData);
}

//
// ChannelArguments
//
NATIVE_DATA_CLASS(GrpcChannelArguments, GrpcNative\\ChannelArguments,
                  std::shared_ptr<ChannelArguments>, std::move(data));
NATIVE_SET_METHOD(GrpcChannelArguments, SetMaxReceiveMessageSize,
                  d->data_->SetMaxReceiveMessageSize(size), int size);
NATIVE_SET_METHOD(GrpcChannelArguments, SetMaxSendMessageSize,
                  d->data_->SetMaxSendMessageSize(size), int size);
NATIVE_SET_METHOD(GrpcChannelArguments, SetLoadBalancingPolicyName,
                  d->data_->SetLoadBalancingPolicyName(s.toCppString()),
                  const String &s);
NATIVE_SET_METHOD(GrpcChannelArguments, SetServiceConfigJSON,
                  d->data_->SetServiceConfigJSON(s.toCppString()),
                  const String &s);
NATIVE_GET_METHOD(String, GrpcChannelArguments, DebugNormalized,
                  d->data_->DebugNormalized());

Object HHVM_STATIC_METHOD(GrpcChannelArguments, Create) {
  return GrpcChannelArguments::newInstance(std::move(ChannelArguments::New()));
}
Object HHVM_STATIC_METHOD(GrpcChannel, Create, const String &name,
                          const String &target, const Object &args) {
  auto *dca = Native::data<GrpcChannelArguments>(args);
  return GrpcChannel::newInstance(std::move(
      GetChannel(name.toCppString(), target.toCppString(), dca->data_)));
}

//
// Housekeeping
//
String HHVM_FUNCTION(Version) { return Version(); }
String HHVM_FUNCTION(DebugAllChannels) { return DebugAllChannels(); }

struct GrpcExtension : Extension {
  GrpcExtension() : Extension("grpc", "0.0.3") { Init(); }

  void moduleInit() override {
    // Status
    HHVM_MALIAS(GrpcNative\\Status, Code, GrpcStatus, Code);
    HHVM_MALIAS(GrpcNative\\Status, Message, GrpcStatus, Message);
    HHVM_MALIAS(GrpcNative\\Status, Details, GrpcStatus, Details);
    Native::registerNativeDataInfo<GrpcStatus>(
        GrpcStatus::s_cppClassName.get());

    // ClientContext
    HHVM_STATIC_MALIAS(GrpcNative\\ClientContext, Create, GrpcClientContext,
                       Create);
    HHVM_MALIAS(GrpcNative\\ClientContext, Peer, GrpcClientContext, Peer);
    HHVM_MALIAS(GrpcNative\\ClientContext, SetTimeoutMicros, GrpcClientContext,
                SetTimeoutMicros);
    HHVM_MALIAS(GrpcNative\\ClientContext, AddMetadata, GrpcClientContext,
                AddMetadata);
    Native::registerNativeDataInfo<GrpcClientContext>(
        GrpcClientContext::s_cppClassName.get());

    // ChannelArguments
    HHVM_STATIC_MALIAS(GrpcNative\\ChannelArguments, Create,
                       GrpcChannelArguments, Create);
    HHVM_MALIAS(GrpcNative\\ChannelArguments, SetMaxReceiveMessageSize,
                GrpcChannelArguments, SetMaxReceiveMessageSize);
    HHVM_MALIAS(GrpcNative\\ChannelArguments, SetMaxSendMessageSize,
                GrpcChannelArguments, SetMaxSendMessageSize);
    HHVM_MALIAS(GrpcNative\\ChannelArguments, SetLoadBalancingPolicyName,
                GrpcChannelArguments, SetLoadBalancingPolicyName);
    HHVM_MALIAS(GrpcNative\\ChannelArguments, SetServiceConfigJSON,
                GrpcChannelArguments, SetServiceConfigJSON);
    HHVM_MALIAS(GrpcNative\\ChannelArguments, DebugNormalized,
                GrpcChannelArguments, DebugNormalized);
    Native::registerNativeDataInfo<GrpcChannelArguments>(
        GrpcChannelArguments::s_cppClassName.get());

    // StreamReader
    HHVM_MALIAS(GrpcNative\\StreamReader, Next, GrpcStreamReader, Next);
    HHVM_MALIAS(GrpcNative\\StreamReader, Response, GrpcStreamReader, Response);
    HHVM_MALIAS(GrpcNative\\StreamReader, Status, GrpcStreamReader, Status);
    Native::registerNativeDataInfo<GrpcStreamReader>(
        GrpcStreamReader::s_cppClassName.get());

    // Channel
    HHVM_STATIC_MALIAS(GrpcNative\\Channel, Create, GrpcChannel, Create);
    HHVM_MALIAS(GrpcNative\\Channel, UnaryCall, GrpcChannel, UnaryCall);
    HHVM_MALIAS(GrpcNative\\Channel, ServerStreamingCall, GrpcChannel,
                ServerStreamingCall);
    HHVM_MALIAS(GrpcNative\\Channel, Debug, GrpcChannel, Debug);
    Native::registerNativeDataInfo<GrpcChannel>(
        GrpcChannel::s_cppClassName.get());

    // Housekeeping.
    HHVM_FALIAS(GrpcNative\\Version, Version);
    HHVM_FALIAS(GrpcNative\\DebugAllChannels, DebugAllChannels);

    loadSystemlib();
  }
} s_grpc_extension;

HHVM_GET_MODULE(grpc);

} // namespace HPHP
