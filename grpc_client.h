#ifndef GRPC_CLIENT_H__
#define GRPC_CLIENT_H__

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

//
// Status
//

struct Status {
  Status();
  // todo constructor
  int code_;
  std::string message_;
  std::string details_;
  bool Ok() const;
};

//
// SerDe
//

typedef std::vector<std::pair<const uint8_t *, size_t>> SliceList;
struct Response {
  virtual Status Slices(SliceList *) = 0;
  virtual ~Response(){};
};

struct Deserializer {
  virtual void ResponseReady(std::unique_ptr<Response>) = 0;
  virtual ~Deserializer(){};
};
struct Serializer {
  // Invoked when the request content is being serialized to the wire.
  virtual void FillRequest(const void **, size_t *) const = 0;
  virtual ~Serializer(){};
};

//
// Metadata and context
//

typedef std::unordered_map<std::string, std::vector<std::string>> Md;

struct ClientContext {
  static std::shared_ptr<ClientContext> New();
  virtual void SetTimeoutMicros(int to) = 0;
  virtual void AddMetadata(const std::string &k, const std::string &v) = 0;
  virtual std::string Peer() = 0;
  virtual void TryCancel() = 0;
  virtual ~ClientContext(){};
};

//
// Callbacks.
//

struct UnaryResultEvents : Deserializer {
  // Invoked on completion.
  virtual void Done(const Status &s) = 0;
  virtual ~UnaryResultEvents(){};
};

struct StreamReadEvents : Deserializer {
  virtual void Done(const Status &s, bool success) = 0;
  virtual ~StreamReadEvents(){};
};

struct StreamReader {
  virtual void Next(StreamReadEvents *d) = 0;
  virtual ~StreamReader(){};
};

//
// Channels
//

struct Channel {
  // todo reorder to ctx up front
  virtual void UnaryCall(const std::string &method,
                         std::shared_ptr<ClientContext> ctx,
                         std::shared_ptr<Serializer> req_,
                         UnaryResultEvents *) = 0;
  virtual std::unique_ptr<StreamReader>
  ServerStreamingCall(const std::string &method,
                      std::shared_ptr<Serializer> req_,
                      std::shared_ptr<ClientContext> ctx) = 0;
  virtual std::string Debug() = 0;
  virtual ~Channel(){};
};

//
// ChannelArguments
//
struct ChannelArguments {
  static std::shared_ptr<ChannelArguments> New();
  virtual void SetMaxReceiveMessageSize(int size) = 0;
  virtual void SetMaxSendMessageSize(int size) = 0;
  virtual void SetLoadBalancingPolicyName(const std::string &lb) = 0;
  virtual void SetServiceConfigJSON(const std::string &name) = 0;
  virtual std::string DebugNormalized() = 0;

  virtual ~ChannelArguments(){};
};

std::shared_ptr<Channel> GetChannel(const std::string &name,
                                    const std::string &target,
                                    std::shared_ptr<ChannelArguments> args);

//
// Housekeeping.
//

void Init();

std::string Version();

std::string DebugAllChannels();

#endif // GRPC_CLIENT_H__
