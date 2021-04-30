<?hh // strict

<<__EntryPoint>>
function main(): void {
  echo "test start\n";
  echo "version ".GrpcNative\Version()."\n";
  $cargs = GrpcNative\ChannelArguments::Create();
  $cargs->SetMaxSendMessageSize(100000000);
  echo $cargs->DebugNormalized()."\n";
  $channel = GrpcNative\Channel::Create('foo', 'localhost:60000', $cargs);
  echo GrpcNative\DebugAllChannels()."\n";
  while (true) {
    $ctx = GrpcNative\ClientContext::Create();
    list($r, $s) = HH\Asio\join(
      $channel->UnaryCall(
        $ctx,
        '/helloworld.HelloWorldService/SayHello',
        'bonjour!',
      ),
    );
    //$r = HH\Asio\join(grpc_unary_call('hello world'));
    echo "code: {$s->Code()}\n";
    echo "message: '{$s->Message()}'\n";
    echo "response length: ".strlen($r)."\n";
    echo "response: '{$r}'\n";
    for ($i = 0; $i < strlen($r); $i++) {
      if ($r[$i] != '!') {
        if ($i != 0 && $i + 1 != strlen($r)) {
          throw new \Exception("poop stick");
        }
      }
    }
    echo "peer: '".$ctx->Peer()."'\n";
    if ($ctx->Peer() != "ipv4:127.0.0.1:60000") {
      throw new \Exception('shit');
    }
  }
  echo $channel->Debug()."\n";
  $ctx = GrpcNative\ClientContext::Create();
  $reader = $channel->ServerStreamingCall(
    $ctx,
    '/helloworld.HelloWorldService/SayHelloStream',
    '',
  );
  echo "streaming...\n";
  while (HH\Asio\join($reader->Next())) {
    $r = $reader->Response();
    echo "response length: ".strlen($r)."\n";
    echo "response: '{$r}'\n";
  }
  $s = $reader->Status();
  echo "code: {$s->Code()}\n";
  echo "message: '{$s->Message()}'\n";

  echo "test fin\n";
}
