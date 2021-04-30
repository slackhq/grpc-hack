package main

import (
	"fmt"
	"google.golang.org/grpc"
	"google.golang.org/grpc/metadata"
	"math/rand"
	"net"
	"reflect"
	"strings"
)

func check(err error) {
	if err != nil {
		panic(err)
	}
}

func main() {
	grpcServer := grpc.NewServer(
		grpc.CustomCodec(&PassThroughCodec{}),
		grpc.UnknownServiceHandler(Handler),
	)
	lis, err := net.Listen("tcp", fmt.Sprintf("localhost:%d", 60000))
	check(err)
	grpcServer.Serve(lis)
	fmt.Println("vim-go")

}

func Handler(_ interface{}, stream grpc.ServerStream) error {
	ctx := stream.Context()
	ts := grpc.ServerTransportStreamFromContext(ctx)
	fmt.Println("method", ts.Method())
	md, _ := metadata.FromIncomingContext(ctx)
	fmt.Printf("metadata %+v\n", md)
	var req []byte
	err := stream.RecvMsg(&req)
	fmt.Println("request", string(req))
	check(err)
	//resp := []byte("lol hey dude!")
	resp := randMessage()
	fmt.Println("response length is ", len(resp))
	err = stream.SendMsg(resp)
	check(err)
	return nil
}

func randMessage() []byte {
	return []byte("~" + strings.Repeat("!", rand.Intn(8000)) + "~")
}

type PassThroughCodec struct{}

func (c *PassThroughCodec) Marshal(v interface{}) ([]byte, error) {
	out, ok := v.([]byte)
	if !ok {
		return nil, fmt.Errorf("pass_through_codec: Marshal called with %v expected []byte", reflect.TypeOf(v))
	}
	return out, nil
}

func (c *PassThroughCodec) Unmarshal(data []byte, v interface{}) error {
	out, ok := v.(*[]byte)
	if !ok {
		return fmt.Errorf("pass_through_codec: Unmarshal called with %v exected *bytes.Buffer", reflect.TypeOf(v))
	}
	*out = data
	return nil
}

func (c *PassThroughCodec) String() string {
	return "PassthroughCodec"
}

func (c *PassThroughCodec) Name() string {
	return c.String()
}
