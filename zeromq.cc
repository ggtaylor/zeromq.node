/*
 * Copyright (c) 2010 Justin Tulloss
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "zeromq.h"

namespace zmq {

static Persistent<String> receive_symbol;
static Persistent<String> error_symbol;
static Persistent<String> connect_symbol;

void
Context::Initialize (v8::Handle<v8::Object> target) {
    HandleScope scope;

    Local<FunctionTemplate> t = FunctionTemplate::New(New);

    t->Inherit(EventEmitter::constructor_template);
    t->InstanceTemplate()->SetInternalFieldCount(1);

    NODE_SET_PROTOTYPE_METHOD(t, "close", Close);

    target->Set(String::NewSymbol("Context"), t->GetFunction());
}

void *
Context::getCContext() {
    return context_;
}

void
Context::AddSocket(Socket *s) {
    sockets_.push_back(s);
    zmq_poller_.data = this;
    ev_idle_start(EV_DEFAULT_UC_ &zmq_poller_);
}

void
Context::RemoveSocket(Socket *s) {
    sockets_.remove(s);
    if (sockets_.empty()) {
        ev_idle_stop(EV_DEFAULT_UC_ &zmq_poller_);
    }
}

void
Context::Close() {
    zmq_term(context_);
    context_ = NULL;
    Unref();
}

Handle<Value>
Context::New (const Arguments& args) {
    HandleScope scope;

    Context *context = new Context();
    context->Wrap(args.This());

    return args.This();
}

Handle<Value>
Context::Close (const Arguments& args) {
    zmq::Context *context = ObjectWrap::Unwrap<Context>(args.This());
    HandleScope scope;
    context->Close();
    return Undefined();
}

Context::Context () : EventEmitter () {
    context_ = zmq_init(1);
    ev_idle_init(&zmq_poller_, DoPoll);
}

Context::~Context () {
    assert(context_ == NULL);
}

void
Context::DoPoll(EV_P_ ev_idle *watcher, int revents) {
    std::list<Socket *>::iterator s;
    assert(revents == EV_IDLE);

    Context *c = (Context *) watcher->data;

    int i = -1;

    zmq_pollitem_t *pollers = (zmq_pollitem_t *)
        malloc(c->sockets_.size() * sizeof(zmq_pollitem_t));
    if (!pollers) return;

    for (s = c->sockets_.begin(); s != c->sockets_.end(); s++) {
        i++;
        pollers[i].socket = (*s)->socket_;
        pollers[i].events = (*s)->events_;
    }

    zmq_poll(pollers, i + 1, 0); // Return instantly w/timeout 0

    i = -1;

    for (s = c->sockets_.begin(); s != c->sockets_.end(); s++) {
        i++;
        (*s)->AfterPoll(pollers[i].revents);
    }

    free(pollers);
}


void
Socket::Initialize (v8::Handle<v8::Object> target) {
    HandleScope scope;

    Local<FunctionTemplate> t = FunctionTemplate::New(New);

    t->Inherit(EventEmitter::constructor_template);
    t->InstanceTemplate()->SetInternalFieldCount(1);

    NODE_DEFINE_CONSTANT(t, ZMQ_PUB);
    NODE_DEFINE_CONSTANT(t, ZMQ_SUB);
    NODE_DEFINE_CONSTANT(t, ZMQ_REQ);
    NODE_DEFINE_CONSTANT(t, ZMQ_REP);
    NODE_DEFINE_CONSTANT(t, ZMQ_UPSTREAM);
    NODE_DEFINE_CONSTANT(t, ZMQ_DOWNSTREAM);
    NODE_DEFINE_CONSTANT(t, ZMQ_PAIR);

    NODE_SET_PROTOTYPE_METHOD(t, "bind", Bind);
    NODE_SET_PROTOTYPE_METHOD(t, "connect", Connect);
    NODE_SET_PROTOTYPE_METHOD(t, "subscribe", Subscribe);
    NODE_SET_PROTOTYPE_METHOD(t, "unsubscribe", Unsubscribe);
    NODE_SET_PROTOTYPE_METHOD(t, "getHighWaterMark", GetHighWaterMark);
    NODE_SET_PROTOTYPE_METHOD(t, "setHighWaterMark", SetHighWaterMark);
    NODE_SET_PROTOTYPE_METHOD(t, "getDiskOffloadSize", GetDiskOffloadSize);
    NODE_SET_PROTOTYPE_METHOD(t, "setDiskOffloadSize", SetDiskOffloadSize);
    NODE_SET_PROTOTYPE_METHOD(t, "setIdentity", SetIdentity);
    NODE_SET_PROTOTYPE_METHOD(t, "getIdentity", GetIdentity);
    NODE_SET_PROTOTYPE_METHOD(t, "getMulticastDataRate", GetMulticastDataRate);
    NODE_SET_PROTOTYPE_METHOD(t, "setMulticastDataRate", SetMulticastDataRate);
    NODE_SET_PROTOTYPE_METHOD(t, "getRecoveryIVL", GetRecoveryIVL);
    NODE_SET_PROTOTYPE_METHOD(t, "setRecoveryIVL", SetRecoveryIVL);
    NODE_SET_PROTOTYPE_METHOD(t, "hasMulticastLoop", HasMulticastLoop);
    NODE_SET_PROTOTYPE_METHOD(t, "setMulticastLoop", SetMulticastLoop);
    NODE_SET_PROTOTYPE_METHOD(t, "getTransmitBufferSize", GetTransmitBufferSize);
    NODE_SET_PROTOTYPE_METHOD(t, "setTransmitBufferSize", SetTransmitBufferSize);
    NODE_SET_PROTOTYPE_METHOD(t, "getReceiveBufferSize", GetReceiveBufferSize);
    NODE_SET_PROTOTYPE_METHOD(t, "setReceiveBufferSize", SetReceiveBufferSize);
    NODE_SET_PROTOTYPE_METHOD(t, "send", Send);
    NODE_SET_PROTOTYPE_METHOD(t, "close", Close);

    receive_symbol = NODE_PSYMBOL("receive");
    connect_symbol = NODE_PSYMBOL("connect");
    error_symbol = NODE_PSYMBOL("error");

    target->Set(String::NewSymbol("Socket"), t->GetFunction());
}

int
Socket::Bind(const char *address) {
    return zmq_bind(socket_, address);
}

int
Socket::Connect(const char *address) {
    return zmq_connect(socket_, address);
}

Handle<Value> 
Socket::GetLongSockOpt(int option, const Arguments &args) {
    int64_t value = 0;
    size_t len = sizeof(value);
    zmq_getsockopt(socket_, option, &value, &len);
    return v8::Integer::New(value); // WARNING: long cast to int!
}

Handle<Value> 
Socket::SetLongSockOpt(int option, const Arguments &args) {
    if (!args[0]->IsNumber()) {
        return ThrowException(Exception::TypeError(
            String::New("Value must be an integer")));
    }
    int64_t value = (int64_t)args[0]->ToInteger()->Value(); // WARNING: int cast to long!
    zmq_setsockopt(socket_, option, &value, sizeof(value));
    return Undefined();
}

Handle<Value> 
Socket::GetULongSockOpt(int option, const Arguments &args) {
    uint64_t value = 0;
    size_t len = sizeof(value);
    zmq_getsockopt(socket_, option, &value, &len);
    return v8::Integer::New(value); // WARNING: long cast to int!
}

Handle<Value> 
Socket::SetULongSockOpt(int option, const Arguments &args) {
    if (!args[0]->IsNumber()) {
        return ThrowException(Exception::TypeError(
            String::New("Value must be an integer")));
    }
    uint64_t value = (uint64_t)args[0]->ToInteger()->Value(); // WARNING: int cast to long!
    zmq_setsockopt(socket_, option, &value, sizeof(value));
    return Undefined();
}

Handle<Value> 
Socket::GetBytesSockOpt(int option, const Arguments &args) {
    char value[1024] = {0};
    size_t len = 1023;
    zmq_getsockopt(socket_, option, value, &len);
    return v8::String::New(value);
}

Handle<Value> 
Socket::SetBytesSockOpt(int option, const Arguments &args) {
    if (!args[0]->IsString()) {
        return ThrowException(Exception::TypeError(
            String::New("Value must be a string!")));
    }    
    String::Utf8Value value(args[0]->ToString());
    zmq_setsockopt(socket_, option, *value, value.length());
    return Undefined();
}

int
Socket::Send(char *msg, int length, int flags, void* hint) {
    int rc;
    zmq_msg_t z_msg;
    rc = zmq_msg_init_data(&z_msg, msg, length, FreeMessage, hint);
    if (rc < 0) {
        return rc;
    }

    rc = zmq_send(socket_, &z_msg, flags);
    if (rc < 0) {
        return rc;
    }

    return zmq_msg_close(&z_msg);
}

int
Socket::Recv(int flags, zmq_msg_t* z_msg) {
    int rc;
    rc = zmq_msg_init(z_msg);
    if (rc < 0) {
        return rc;
    }
    return zmq_recv(socket_, z_msg, flags);
}

void
Socket::Close() {
    zmq_close(socket_);
    context_->RemoveSocket(this);
    socket_ = NULL;
    Unref();
}

const char *
Socket::ErrorMessage() {
    return zmq_strerror(zmq_errno());
}

Handle<Value>
Socket::New (const Arguments &args) {
    HandleScope scope;
    if (args.Length() != 2) {
        return ThrowException(Exception::Error(
            String::New("Must pass a context and a type to constructor")));
    }
    Context *context = ObjectWrap::Unwrap<Context>(args[0]->ToObject());
    if (!args[1]->IsNumber()) {
        return ThrowException(Exception::TypeError(
            String::New("Type must be an integer")));
    }
    int type = (int) args[1]->ToInteger()->Value();

    Socket *socket = new Socket(context, type);
    socket->Wrap(args.This());

    return args.This();
}

Handle<Value>
Socket::Connect (const Arguments &args) {
    HandleScope scope;
    Socket *socket = getSocket(args);
    if (!args[0]->IsString()) {
        return ThrowException(Exception::TypeError(
            String::New("Address must be a string!")));
    }

    String::Utf8Value address(args[0]->ToString());
    if (socket->Connect(*address)) {
        return ThrowException(Exception::Error(
            String::New(socket->ErrorMessage())));
    }
    return Undefined();
}

Handle<Value> 
Socket::Subscribe (const Arguments &args) {
    return getSocket(args)->SetBytesSockOpt(ZMQ_SUBSCRIBE, args);
}

Handle<Value> 
Socket::Unsubscribe (const Arguments &args) {
    return getSocket(args)->SetBytesSockOpt(ZMQ_UNSUBSCRIBE, args);
}

Handle<Value> 
Socket::GetHighWaterMark (const Arguments &args) {
    return getSocket(args)->GetULongSockOpt(ZMQ_HWM, args);
}

Handle<Value> 
Socket::SetHighWaterMark (const Arguments &args) {
    return getSocket(args)->SetULongSockOpt(ZMQ_HWM, args);
}

Handle<Value> 
Socket::GetDiskOffloadSize (const Arguments &args) {
    return getSocket(args)->GetLongSockOpt(ZMQ_SWAP, args);
}

Handle<Value> 
Socket::SetDiskOffloadSize (const Arguments &args) {
    return getSocket(args)->SetLongSockOpt(ZMQ_SWAP, args);
}

Handle<Value> 
Socket::GetIdentity (const Arguments &args) {
    return getSocket(args)->GetBytesSockOpt(ZMQ_IDENTITY, args);
}

Handle<Value> 
Socket::SetIdentity (const Arguments &args) {
    return getSocket(args)->SetBytesSockOpt(ZMQ_IDENTITY, args);
}

Handle<Value> 
Socket::GetMulticastDataRate (const Arguments &args) {
    return getSocket(args)->GetLongSockOpt(ZMQ_RATE, args);
}

Handle<Value> 
Socket::SetMulticastDataRate (const Arguments &args) {
    return getSocket(args)->SetLongSockOpt(ZMQ_RATE, args);
}

Handle<Value> 
Socket::GetRecoveryIVL (const Arguments &args) {
    return getSocket(args)->GetLongSockOpt(ZMQ_RECOVERY_IVL, args);
}

Handle<Value> 
Socket::SetRecoveryIVL (const Arguments &args) {
    return getSocket(args)->SetLongSockOpt(ZMQ_RECOVERY_IVL, args);
}

Handle<Value> 
Socket::HasMulticastLoop (const Arguments &args) {
    return getSocket(args)->GetLongSockOpt(ZMQ_MCAST_LOOP, args);
}

Handle<Value> 
Socket::SetMulticastLoop (const Arguments &args) {
    return getSocket(args)->SetLongSockOpt(ZMQ_MCAST_LOOP, args);
}

Handle<Value> 
Socket::GetTransmitBufferSize (const Arguments &args) {
    return getSocket(args)->GetULongSockOpt(ZMQ_SNDBUF, args);
}

Handle<Value> 
Socket::SetTransmitBufferSize (const Arguments &args) {
    return getSocket(args)->SetULongSockOpt(ZMQ_SNDBUF, args);
}

Handle<Value> 
Socket::GetReceiveBufferSize (const Arguments &args) {
    return getSocket(args)->GetULongSockOpt(ZMQ_RCVBUF, args);
}

Handle<Value> 
Socket::SetReceiveBufferSize (const Arguments &args) {
    return getSocket(args)->SetULongSockOpt(ZMQ_RCVBUF, args);
}

Handle<Value>
Socket::Bind (const Arguments &args) {
    HandleScope scope;
    Socket *socket = getSocket(args);
    Local<Function> cb = Local<Function>::Cast(args[1]);
    if (!args[0]->IsString()) {
        return ThrowException(Exception::TypeError(
            String::New("Address must be a string!")));
    }

    if (args.Length() > 1 && !args[1]->IsFunction()) {
        return ThrowException(Exception::TypeError(
            String::New("Provided callback must be a function")));
    }

    socket->bindCallback_ = Persistent<Function>::New(cb);
    socket->bindAddress_ = Persistent<String>::New(args[0]->ToString());
    eio_custom(EIO_DoBind, EIO_PRI_DEFAULT, EIO_BindDone, socket);

    socket->Ref(); // Reference ourself until the callback is done
    ev_ref(EV_DEFAULT_UC);

    return Undefined();
}

int
Socket::EIO_BindDone(eio_req *req) {
    HandleScope scope;

    ev_unref(EV_DEFAULT_UC);

    Socket *socket = (Socket *) req->data;

    TryCatch try_catch;

    Local<Value> argv[1];

    if (socket->bindError_) {
        argv[0] = String::New(socket->ErrorMessage());
    }
    else {
        argv[0] = String::New("");
    }
    socket->bindCallback_->Call(v8::Context::GetCurrent()->Global(), 1, argv);

    if (try_catch.HasCaught()) {
        FatalException(try_catch);
    }

    socket->bindAddress_.Dispose();
    socket->bindCallback_.Dispose();

    socket->Unref();

    return 0;
}

int
Socket::EIO_DoBind(eio_req *req) {
    Socket *socket = (Socket *) req->data;
    String::Utf8Value address(socket->bindAddress_);
    socket->bindError_ = socket->Bind(*address);
    return 0;
}

Handle<Value>
Socket::Send (const Arguments &args) {
    HandleScope scope;
    Socket *socket = getSocket(args);
    if (!args.Length() == 1) {
        return ThrowException(Exception::TypeError(
            String::New("Must pass in a string to send")));
    }

    socket->QueueOutgoingMessage(args[0]);

    return Undefined();
}

Handle<Value>
Socket::Close (const Arguments &args) {
    HandleScope scope;

    Socket *socket = getSocket(args);
    socket->Close();
    return Undefined();
}

Socket::Socket (Context *context, int type) : EventEmitter () {
    socket_ = zmq_socket(context->getCContext(), type);
    events_ = ZMQ_POLLIN;
    context_ = context;
    context_->AddSocket(this);
}

Socket::~Socket () {
    assert(socket_ == NULL);
}

void
Socket::QueueOutgoingMessage(Local <Value> message) {
    Persistent<Value> p_message = Persistent<Value>::New(message);
    events_ |= ZMQ_POLLOUT;
    outgoing_.push_back(p_message);
}

void
Socket::FreeMessage(void *data, void *message) {
    String::Utf8Value *js_msg = (String::Utf8Value *)message;
    delete js_msg;
}

Socket *
Socket::getSocket(const Arguments &args) {
    return ObjectWrap::Unwrap<Socket>(args.This());
}

int
Socket::AfterPoll(int revents) {
    HandleScope scope;

    Local <Value> exception;

    if (revents & ZMQ_POLLIN) {
        zmq_msg_t z_msg;
        if (Recv(0, &z_msg)) {
            Emit(error_symbol, 1, &exception);
        }
        Local <Value>js_msg = String::New(
            (char *) zmq_msg_data(&z_msg),
            zmq_msg_size(&z_msg));
        Emit(receive_symbol, 1, &js_msg);
        zmq_msg_close(&z_msg);

    }

    if (revents & ZMQ_POLLOUT && !outgoing_.empty()) {
        String::Utf8Value *message = new String::Utf8Value(outgoing_.front());
        if (Send(**message, message->length(), 0, (void *) message)) {
            exception = Exception::Error(
                String::New(ErrorMessage()));
            Emit(receive_symbol, 1, &exception);
        }

        outgoing_.front().Dispose();
        outgoing_.pop_front();
    }

    return 0;
}

}

extern "C" void
init (Handle<Object> target) {
    HandleScope scope;
    zmq::Context::Initialize(target);
    zmq::Socket::Initialize(target);
}
