//-----------------------------------------------------------------
// TIBCO EMS Client - Node.JS wrapper / EmsClientWrapper class
// File emsclient.cc
// 03/01/2017
// Copyright (c) 2017-2017 by Information Design One AG
//-----------------------------------------------------------------

//---------------------------NOTE----------------------------------
// javascript callback functions can ONLY be called from node mainthread.
// BUT I/O shall be done in a background thread for it does NOT block node mainthread.
// therefore sending/receiving/open/close is decoupled with a worker-thread (EmsClientThread).
// communication between mainthread + emsthread is done via a) WorkList (<->) b) libuv's async_send() (<-)
//-----------------------------------------------------------------

#include "emsclient.hpp"

#include <node.h>
#include <uv.h>                   // libuv
#include <string>
#include <pthread.h>

//-----------------------------------------------------------------
// globals
//-----------------------------------------------------------------

WorkList<Work*> pendingSendWork;     // write: mainthread  read: emsthread
WorkList<Work*> doneSendWork;        // write: emsthread   read: mainthread
WorkList<Work*> pendingReceiveWork;  // write: emsthread   read: mainthread
WorkList<Control*> controlQueue;     // write: mainthread  read: emsthread
WorkList<Control*> controlQueueDone; // write: emsthread   read: mainthread

uv_async_t onSendType, onMessageType, onOpenCloseType;   // callbacks on mainthread triggered by emsthread

int callbackSet= 0;                     // flag to check if JS code *DID* set a callback function
int closeCallbackSet= 0;                     // flag to check if JS code *DID* set a callback function
int openCallbackSet= 0;                     // flag to check if JS code *DID* set a callback function
Persistent<Function> onMessageCallback; // global javascript callback for message receive
Persistent<Function> onOpenCallback;    // global javascript callback for message receive
Persistent<Function> onCloseCallback;   // global javascript callback for message receive

void fillReceiveOptions(Local<Object>& obj, Isolate* isolate, EmsMessage* message);

//-----------------------------------------------------------------
// SEND workflow
//-----------------------------------------------------------------

// Mainthread -> pendingSendWork.enqueue(Work*) 
// \_
//    EmsThread -> pendingSendWork.dequeue(Work*) + send (TIBCO EMS)
//    EmsThread -> doneSendWork.enqueue(Work*) + async_send()
//    \_
//       Mainthread -> doneSendWork.dequeue() + call JS callback from Work*

//-----------------------------------------------------------------
// RECEIVE workflow
//-----------------------------------------------------------------

// Mainthread -> setMessageHandler(JSCallback)
// 
// EmsThread -> receive (TIBCO EMS)
// EmsThread -> pendingReceiveWork.enQueue(Work*) + async_send()
// \_
//    Mainthread -> pendingReceiveWork.dequeue() + call *GLOBAL* JS callback

//-----------------------------------------------------------------
// OPEN/CLOSE workflow
//-----------------------------------------------------------------

// Mainthread -> controlQueue.enqueue(Control*)
// \_
//    EmsThread -> open/close async (TIBCO EMS)
//    EmsThread -> controlQueueDone.enQueue(Work*) + async_send()
//    \_
//       Mainthread -> controlQueueDone.dequeue() + call *GLOBAL* JS callback

//-----------------------------------------------------------------
// async callbacks
//-----------------------------------------------------------------
// called AFTER Producer::send (on MAINTHREAD)
//-----------------------------------------------------------------

void onSend(uv_async_t* handle)
{
   Isolate* isolate= Isolate::GetCurrent();
   HandleScope scope(isolate);
   
   // (1) get done work struct from list (mutex-protected)
   
   Work* work= doneSendWork.dequeue();
   
   while (work)
   {
      // (2) build args

      Local<Object> obj = Object::New(isolate);

      if (work->isRequest)
         fillReceiveOptions(obj, isolate, work->message);

      // (3) execute the callbackSet

      if (work->callbackSet)
      {
         if (work->isRequest)
         {
            Handle<Value> argv[]= 
            {
               work->errorText ? Handle<Value>(v8::String::NewFromUtf8(isolate, work->errorText)) : Handle<Value>(v8::Null(isolate)),
               work->response && work->response->getText() ? Handle<Value>(v8::String::NewFromUtf8(isolate, notNull(work->response->getText()))) : Handle<Value>(v8::Null(isolate)),
               obj
            };

            Local<Function>::New(isolate, work->callback)->Call(isolate->GetCurrentContext()->Global(), 3, argv);
         }
         else
         {
            Handle<Value> argv[]= 
            {
               work->errorText ? Handle<Value>(v8::String::NewFromUtf8(isolate, work->errorText)) : Handle<Value>(v8::Null(isolate))
            };

            Local<Function>::New(isolate, work->callback)->Call(isolate->GetCurrentContext()->Global(), 1, argv);
         }

         // (4) dispose the callback object from the baton and delete baton object itself

         work->callback.Reset();
      }

      delete work;

      // (5) loop until no more work done
      
      work= doneSendWork.dequeue();
   }   
}

//-----------------------------------------------------------------
// called AFTER msgCallback (on MAINTHREAD)
//-----------------------------------------------------------------

void onMessage(uv_async_t* handle)
{
   Isolate* isolate= Isolate::GetCurrent();
   HandleScope scope(isolate);
   
   // (1) get done work struct from list (mutex-protected)
   
   Work* work= pendingReceiveWork.dequeue();
   
   while (work)
   {
      // (2) build args

      Local<Object> obj = Object::New(isolate);
      fillReceiveOptions(obj, isolate, work->message);

      // cb("message", { header: { messageID: "asodk12" }, properties: { someProp: "asd" }})

      if (callbackSet)
      {
         Handle<Value> argv[] =
         {
            Handle<Value>(v8::String::NewFromUtf8(isolate, work->message ? work->message->getText() : "")),
            obj
         };
      
         // (3) execute the global onMessage callback function (set by JS via setMessageHandler())

         Local<Function>::New(isolate, onMessageCallback)->Call(isolate->GetCurrentContext()->Global(), 2, argv);
      }
      else
         printf("FATAL: No message handler callback given, cannot process message!\n");
      
      delete work;
      
      // (5) loop until no more work done
      
      work= pendingReceiveWork.dequeue();
   }   
}

//-----------------------------------------------------------------
// called AFTER async OPEN (on MAINTHREAD)
//-----------------------------------------------------------------

void onOpenClose(uv_async_t* handle)
{
   Isolate* isolate= Isolate::GetCurrent();
   HandleScope scope(isolate);
   
   // (1) get done work struct from list (mutex-protected)
   
   Control* control= controlQueueDone.dequeue();
   
   while (control)
   {
      // (2) build args

      Handle<Value> argv[] = 
      { 
         control->errorText ? Handle<Value>(v8::String::NewFromUtf8(isolate, control->errorText)) : Handle<Value>(v8::Null(isolate)),
         control->infoText ? Handle<Value>(v8::String::NewFromUtf8(isolate, control->infoText)) : Handle<Value>(v8::Null(isolate))
      };
      
      // (3) execute the global onMessage callback function (set by JS via setMessageHandler())

      if ((control->operation == opOpen && openCallbackSet) || (control->operation == opClose && closeCallbackSet))
         Local<Function>::New(isolate, control->operation == opOpen ? onOpenCallback : onCloseCallback)->Call(isolate->GetCurrentContext()->Global(), 2, argv);

      if (control->operation == opOpen)
         openCallbackSet = 0;
      else
      {
         closeCallbackSet = 0;

         control->wrapper->thread->stop();
         delete control->wrapper->thread;
         control->wrapper->thread = 0;

         uv_close((uv_handle_t*)&onSendType, NULL);
         uv_close((uv_handle_t*)&onMessageType, NULL);
         uv_close((uv_handle_t*)&onOpenCloseType, NULL);
      }
      
      delete control;
      
      // (5) loop until no more work done
      
      control= controlQueueDone.dequeue();
   }   
}

//-----------------------------------------------------------------
// fillReceiveOptions
//-----------------------------------------------------------------

void fillReceiveOptions(Local<Object>& obj, Isolate* isolate, EmsMessage* message)
{
   if (!isolate || !message)
      return;

   Local<Object> header = Object::New(isolate);
   Local<Object> properties = Object::New(isolate);
   const char* headerVal= 0;
   long longVal= 0;
   int intVal= 0;
   bool boolVal = false;

   // extract header parameters and properties of the EMS message and build JS object

   for (const char* prop = message ? message->getFirstPropertyName() : 0; prop;
         prop = message->getNextPropertyName())
   {
      properties->Set(String::NewFromUtf8(isolate, notNull(prop)), String::NewFromUtf8(isolate, notNull(message->getPropertyValue(prop)))); 
   }

   if ((headerVal = message->getMessageID()))
      header->Set(String::NewFromUtf8(isolate, "messageID"), String::NewFromUtf8(isolate, headerVal));
   if ((headerVal = message->getCorrelationID()))
      header->Set(String::NewFromUtf8(isolate, "correlationID"), String::NewFromUtf8(isolate, headerVal));
   if ((headerVal = message->getDeliveryMode()))
      header->Set(String::NewFromUtf8(isolate, "deliveryMode"), String::NewFromUtf8(isolate, headerVal));
   if ((headerVal = message->getDestination()))
      header->Set(String::NewFromUtf8(isolate, "destination"), String::NewFromUtf8(isolate, headerVal));
   if ((headerVal = message->getEncoding()))
      header->Set(String::NewFromUtf8(isolate, "encoding"), String::NewFromUtf8(isolate, headerVal));
   if ((headerVal = message->getReplyTo()))
      header->Set(String::NewFromUtf8(isolate, "replyTo"), String::NewFromUtf8(isolate, headerVal));
   if ((headerVal = message->getTimestamp()))
      header->Set(String::NewFromUtf8(isolate, "timestamp"), String::NewFromUtf8(isolate, headerVal));
   if ((longVal = message->getExpiration()))
      header->Set(String::NewFromUtf8(isolate, "expiration"), Number::New(isolate, longVal));
   if ((intVal = message->getPriority()))
      header->Set(String::NewFromUtf8(isolate, "priority"), Integer::New(isolate, intVal));
   if ((boolVal = message->getRedelivered()))
      header->Set(String::NewFromUtf8(isolate, "priority"), Boolean::New(isolate, boolVal));

   obj->Set(String::NewFromUtf8(isolate, "header"), header);
   obj->Set(String::NewFromUtf8(isolate, "properties"), properties);
}

//-----------------------------------------------------------------
// class EmsClientThread
//-----------------------------------------------------------------
//-----------------------------------------------------------------
// constructors
//-----------------------------------------------------------------

EmsClientThread::EmsClientThread(int silent, int strict)
   : IdThread(silent, strict)
{
   factory= 0;
   connection= 0;
   session= 0;
   consumer= 0;
   producer= 0;
   requestor= 0;

   isOpen= no;
}

EmsClientThread::~EmsClientThread()
{
   close();
}

//-----------------------------------------------------------------
// open
//-----------------------------------------------------------------

int EmsClientThread::open(Control* ctrl)
{
   int res= success;

   if (isOpen)
   {
      ctrl->errorText = strdup("Connection already open");
      ctrl->res= fail;
      return fail;
   }

   if (!ctrl || !ctrl->serverUrl.length())
      return fail;
   
   factory = new EmsConnectionFactory();
   factory->setServerURL(ctrl->serverUrl.c_str());

   if (ctrl->user.length()) factory->setUser(ctrl->user.c_str());
   if (ctrl->password.length()) factory->setPassword(ctrl->password.c_str());
   if (ctrl->certfile.length()) factory->setCertFile(ctrl->certfile.c_str());

   connection = factory->createConnection();

   if (connection->getLastErrorText())
      return error(ctrl, connection);

   session = connection->createSession();

   if (session->getLastErrorText())
      return error(ctrl, session);

   if (ctrl->receiveName.length())
   {
      if (ctrl->isTopic)
      {
         if (ctrl->clientName.length())
            consumer = session->createDurableSubscriber(ctrl->receiveName.c_str(), ctrl->clientName.c_str(), ctrl->selector.c_str());
         else
            consumer = session->createConsumer(TIBEMS_TOPIC, ctrl->receiveName.c_str(), ctrl->selector.c_str(), TIBEMS_FALSE);
      }
      else
         consumer = session->createConsumer(TIBEMS_QUEUE, ctrl->receiveName.c_str(), ctrl->selector.c_str(), TIBEMS_FALSE);

      if (consumer->getLastErrorText())
         return error(ctrl, consumer);

      consumer->setMsgListener(msgCallback, /* this */ 0);

      res= connection->start();

      if (connection->getLastErrorText())
         return error(ctrl, connection);
   }

   if (!res)
   {
      // always instantiate producer, for reply-to responses

      tibemsDeliveryMode delMode = TIBEMS_NON_PERSISTENT;

      if (!ctrl->deliveryMode.compare("persistent")) delMode = TIBEMS_PERSISTENT;
      if (!ctrl->deliveryMode.compare("reliable")) delMode = TIBEMS_RELIABLE;

      producer = session->createProducer(ctrl->isTopic ? TIBEMS_TOPIC : TIBEMS_QUEUE, ctrl->sendName.c_str(), delMode);

      if (producer->getLastErrorText())
         return error(ctrl, producer);
   }

   if (!res && ctrl->requestName.length())
   {
      // instantiate requestor, if request-name is given in options

      requestor= session->createRequestor(ctrl->isTopic ? TIBEMS_TOPIC : TIBEMS_QUEUE, ctrl->requestName.c_str());

      if (requestor->getLastErrorText())
         return error(ctrl, requestor);
   }

   isOpen= yes;

   ctrl->infoText= sstrdup(connection->getConnectionInfo());

   return res;
}

//-----------------------------------------------------------------
// close
//-----------------------------------------------------------------

int EmsClientThread::close()
{
   delete factory;

   factory= 0;
   connection= 0;
   session= 0;
   consumer= 0;
   producer= 0;

   setState(isExit);

   isOpen= no;

   return done;   
}

//-----------------------------------------------------------------
// fail
//-----------------------------------------------------------------

int EmsClientThread::error(Control* ctrl, EmsObject* object)
{
   ctrl->errorText= sstrdup(object->getLastErrorText());
   ctrl->statusText = sstrdup(object->getLastStatusText());
   ctrl->res = object->getLastStatusCode();
   
   return fail;
}

//-----------------------------------------------------------------
// run
//-----------------------------------------------------------------

int EmsClientThread::run()
{
   waitMutex.lock();

   while (!isState(isExit))
   {
      control();
      write();

      if (!isState(isExit))
         waitCond.timedWaitMs(waitMutex, 1000);
   }

   waitMutex.unlock();

   return done;
}

//-----------------------------------------------------------------
// Wake Up
//-----------------------------------------------------------------

void EmsClientThread::wakeUp()
{
   waitMutex.lock();
   waitCond.broadcast();
   waitMutex.unlock();
}

//-----------------------------------------------------------------
// msgCallback
// executed in TIBCO EMS lib background thread context
//-----------------------------------------------------------------

void EmsClientThread::msgCallback(tibemsMsgConsumer msgConsumer, tibemsMsg msg, void* obj)
{
   Work* work= new Work;
   work->message= new EmsMessage(msgConsumer, msg);
   work->res= success;

   pendingReceiveWork.enqueue(work);

   uv_async_send(&onMessageType);
}

//-----------------------------------------------------------------
// control
//-----------------------------------------------------------------

int EmsClientThread::control()
{
   int nDone= 0;
   int res= success;
   Control* ctrl= controlQueue.dequeue();
   
   while (ctrl)
   {
      if (ctrl->operation == opOpen) 
         res= open(ctrl);
      else
         res= close();

      // shift
      
      controlQueueDone.enqueue(ctrl);
         
      nDone++;

      ctrl= controlQueue.dequeue();
   }
   
   if (nDone)
      uv_async_send(&onOpenCloseType);

   return done;
}

//-----------------------------------------------------------------
// write
//-----------------------------------------------------------------

int EmsClientThread::write()
{
   int nSent= 0;
   Work* work= pendingSendWork.dequeue();
   
   while (work)
   {
      if (work->isRequest && requestor)
      {
         work->res= requestor->request(work->message, work->response);
 
         if (work->res)
         {
            work->errorText= sstrdup(requestor->getLastErrorText());
            work->statusText = sstrdup(requestor->getLastStatusText());
            work->res = requestor->getLastStatusCode();
         }
      }
      else if (work->isRequest)
      {
         work->res= fail;
         work->errorText= sstrdup("requestor not initialized");
      }
      else
      {
         work->res= producer->send(work->message);

         if (work->res)
         {
            work->errorText= sstrdup(producer->getLastErrorText());
            work->statusText = sstrdup(producer->getLastStatusText());
            work->res = producer->getLastStatusCode();
         }
      }

      // shift
      
      doneSendWork.enqueue(work);
         
      nSent++;

      work= pendingSendWork.dequeue();
   }
   
   if (nSent)
      uv_async_send(&onSendType);

   return done;
}
      
//-----------------------------------------------------------------
// class EmsClientWrapper
//-----------------------------------------------------------------
//-----------------------------------------------------------------
// constructors
//-----------------------------------------------------------------

Persistent<Function> EmsClientWrapper::constructor;

EmsClientWrapper::EmsClientWrapper()
{
   thread = 0;
}

//-----------------------------------------------------------------
// destructor
//-----------------------------------------------------------------

EmsClientWrapper::~EmsClientWrapper()
{
   if (thread)
      thread->stop();

   delete thread;
}

//-----------------------------------------------------------------
// Init
// Sets up the JS EmsClient class with prototype functions and the
// internal field placeholder in which we will later store the actual 
// ??? object. Also exports the class as "EmsClient"
//-----------------------------------------------------------------

void EmsClientWrapper::Init(Handle<Object> exports)
{
   Isolate* isolate= Isolate::GetCurrent();
 
   // Prepare constructor template
   
   Local<FunctionTemplate> tpl= FunctionTemplate::New(isolate, New);
   tpl->SetClassName(v8::String::NewFromUtf8(isolate, "EmsClient"));
   tpl->InstanceTemplate()->SetInternalFieldCount(1);
 
   // Prototype
   
   NODE_SET_PROTOTYPE_METHOD(tpl, "open", open);
   NODE_SET_PROTOTYPE_METHOD(tpl, "close", close);
   NODE_SET_PROTOTYPE_METHOD(tpl, "send", send);
   NODE_SET_PROTOTYPE_METHOD(tpl, "request", request);
   NODE_SET_PROTOTYPE_METHOD(tpl, "setMessageHandler", setMessageHandler);
 
   // constructor
   
   constructor.Reset(isolate, tpl->GetFunction());
 
   // export as "EmsClient"
   
   exports->Set(v8::String::NewFromUtf8(isolate, "EmsClient"), tpl->GetFunction());
}

//-----------------------------------------------------------------
// New
// Acts as the emsclient.EmsClient constructor function that gets invoked
// when a JS code calls "new emsclient.EmsClient()"
//-----------------------------------------------------------------

void EmsClientWrapper::New(const FunctionCallbackInfo<Value>& args)
{
   Isolate* isolate= Isolate::GetCurrent();
   HandleScope scope(isolate);
 
   // Invoked as constructor: "new EmsClient(...)"
 
   if (args.IsConstructCall())
   {
     EmsClientWrapper* wrapper= new EmsClientWrapper();
     wrapper->Wrap(args.This());
     args.GetReturnValue().Set(args.This());
   }
 
   // Invoked as plain function "EmsClient(...)", turn into constructor call
 
   else
   {
     const int argc= 1;
     Local<Value> argv[argc]= { args[0] };
     Local<Function> cons= Local<Function>::New(isolate, constructor);
     args.GetReturnValue().Set(cons->NewInstance(argc, argv));
   }
}

//-----------------------------------------------------------------
// open
// JS-Object: { 
//   url: 'localhost:7222',               // mandatory
//   user: 'someUserName',
//   password: 'somePass',
//   certFile: 'path/to/something.cert', 
//   selector: 'someSelector', 
//   clientName: 'mySubscriber', 
//   isTopic: true,
//   from: 'some.topic.or.queue',
//   to: 'some.topic.or.queue'
// }
//-----------------------------------------------------------------

void EmsClientWrapper::open(const FunctionCallbackInfo<Value>& args)
{
   Isolate* isolate= Isolate::GetCurrent();
   HandleScope scope(isolate);
   EmsClientWrapper* wrapper= ObjectWrap::Unwrap<EmsClientWrapper>(args.Holder());
  
   if (args.Length() < 1 || !args[0]->IsObject())
   {
      isolate->ThrowException(Exception::TypeError(v8::String::NewFromUtf8(isolate, "Incomplete/missing options object")));
      return;
   }
   
   // register async callbacks
   
   uv_async_init(uv_default_loop(), &onSendType, onSend);
   uv_async_init(uv_default_loop(), &onMessageType, onMessage);
   uv_async_init(uv_default_loop(), &onOpenCloseType, onOpenClose);

   // get options from JS object
  
   Control* control = new Control;
   
   Local<Object> obj= args[0]->ToObject();

   control->operation = opOpen;
   if (has(obj, isolate, "isTopic")) control->isTopic= getInteger(obj, isolate, "isTopic");
   if (has(obj, isolate, "url")) control->serverUrl = getString(obj, isolate, "url");
   if (has(obj, isolate, "user")) control->user = getString(obj, isolate, "user");
   if (has(obj, isolate, "password")) control->password = getString(obj, isolate, "password");
   if (has(obj, isolate, "certfile")) control->certfile = getString(obj, isolate, "certfile");
   if (has(obj, isolate, "selector")) control->selector = getString(obj, isolate, "selector");
   if (has(obj, isolate, "from")) control->receiveName = getString(obj, isolate, "from");
   if (has(obj, isolate, "to")) control->sendName = getString(obj, isolate, "to");
   if (has(obj, isolate, "request")) control->requestName= getString(obj, isolate, "request");
   if (has(obj, isolate, "clientName")) control->clientName = getString(obj, isolate, "clientName");
   if (has(obj, isolate, "deliveryMode")) control->deliveryMode = getString(obj, isolate, "deliveryMode").c_str();

   EmsObject::silent = !getInteger(obj, isolate, "debug");

   // assign callback to global persisten variable

   if (args.Length() >= 2 && args[1]->IsFunction())
   {
      Local<Function> callback= Local<Function>::Cast(args[1]);
      onOpenCallback.Reset(isolate, callback); // differs from example
      openCallbackSet = 1;
   }

   // enqueue Control struct and wake up worker thread in case its sleeping
   
   int countPending= controlQueue.enqueue(control);

   if (!wrapper->thread)
   {
      wrapper->thread= new EmsClientThread(EmsObject::silent);

      if (wrapper->thread->start() != success)
         printf("FATAL: Failed to start communication thread!!!\n");
   }
   else
      wrapper->thread->wakeUp();

   args.GetReturnValue().Set(Number::New(isolate, countPending));
}

//-----------------------------------------------------------------
// close
//-----------------------------------------------------------------

void EmsClientWrapper::close(const FunctionCallbackInfo<Value>& args)
{
   Isolate* isolate= Isolate::GetCurrent();
   HandleScope scope(isolate);
   EmsClientWrapper* wrapper= ObjectWrap::Unwrap<EmsClientWrapper>(args.Holder());

   // get options from JS object
  
   Control* control= new Control;
   control->operation= opClose;
   control->wrapper= wrapper;

   // assign callback to global persisten variable

   if (args.Length() >= 1 && args[0]->IsFunction())
   {
      Local<Function> callback= Local<Function>::Cast(args[0]);
      onCloseCallback.Reset(isolate, callback); // differs from example
      closeCallbackSet = 1;
   }

   // enqueue Control struct and wake up worker thread in case its sleeping

   int countPending= controlQueue.enqueue(control);

   wrapper->thread->wakeUp();

   args.GetReturnValue().Set(Number::New(isolate, countPending));
}

//-----------------------------------------------------------------
// send
// either: send("message", function(err) {})
// or:     send("message", { messageId: "asdsad", ... }, function(err) {})
//-----------------------------------------------------------------

void EmsClientWrapper::send(const FunctionCallbackInfo<Value>& args)
{
  Isolate* isolate= Isolate::GetCurrent();
  HandleScope scope(isolate);
  EmsClientWrapper* wrapper= ObjectWrap::Unwrap<EmsClientWrapper>(args.Holder());
  int res= success;
  
  if (args.Length() < 2)
  {
    isolate->ThrowException(Exception::TypeError(
      v8::String::NewFromUtf8(isolate, "Expected at least 2 argument2 (message, cb)")));
    return;
  }

  if (!args[0]->IsString())
  {
    isolate->ThrowException(Exception::TypeError(v8::String::NewFromUtf8(isolate, "Expected string argument(0)")));
    return;
  }

   Work* work= new Work;
   const std::string m= *v8::String::Utf8Value(args[0]->ToString());

   work->message = new EmsMessage();
   work->message->setText(m.c_str());

   // set any header parameters / properties supplied by the JS options object

   if (args.Length() > 1 && args[1]->IsObject())
   {
      Local<Object> obj= args[1]->ToObject();

      fillSendArguments(obj, isolate, work);
   }

   // callback (second or third arg)
  
   if (args.Length() > 1)
   {
      if ((args.Length() == 2 && args[1]->IsFunction()) || (args.Length() > 2 && args[2]->IsFunction()))
      {

         Local<Function> callback= Local<Function>::Cast(args.Length() > 2 ? args[2] : args[1]);
         work->callback.Reset(isolate, callback);
         work->callbackSet= 1;
      }
   }
  
   // enqueue Work struct and wake up worker thread in case its sleeping

   int countPending= pendingSendWork.enqueue(work);

   wrapper->thread->wakeUp();

   args.GetReturnValue().Set(Number::New(isolate, res ? res : countPending));
}

//-----------------------------------------------------------------
// request
// either: request("message", function(err) {})
// or:     request("message", { messageId: "asdsad", ... }, function(err) {})
//-----------------------------------------------------------------

void EmsClientWrapper::request(const FunctionCallbackInfo<Value>& args)
{
  Isolate* isolate= Isolate::GetCurrent();
  HandleScope scope(isolate);
  EmsClientWrapper* wrapper= ObjectWrap::Unwrap<EmsClientWrapper>(args.Holder());
  int res= success;
  
  if (args.Length() < 2)
  {
    isolate->ThrowException(Exception::TypeError(
      v8::String::NewFromUtf8(isolate, "Expected at least 2 argument2 (message, cb)")));
    return;
  }

  if (!args[0]->IsString())
  {
    isolate->ThrowException(Exception::TypeError(v8::String::NewFromUtf8(isolate, "Expected string argument(0)")));
    return;
  }

   Work* work= new Work;
   const std::string m= *v8::String::Utf8Value(args[0]->ToString());

   work->message = new EmsMessage();
   work->message->setText(m.c_str());
   work->isRequest= yes;

   // set any header parameters / properties supplied by the JS options object

   if (args.Length() > 1 && args[1]->IsObject())
   {
      Local<Object> obj= args[1]->ToObject();

      fillSendArguments(obj, isolate, work);
   }

   // callback (second or third arg)
  
   if (args.Length() > 1)
   {
      if ((args.Length() == 2 && args[1]->IsFunction()) || (args.Length() > 2 && args[2]->IsFunction()))
      {

         Local<Function> callback= Local<Function>::Cast(args.Length() > 2 ? args[2] : args[1]);
         work->callback.Reset(isolate, callback);
         work->callbackSet= 1;
      }
   }
  
   // enqueue Work struct and wake up worker thread in case its sleeping

   int countPending= pendingSendWork.enqueue(work);

   wrapper->thread->wakeUp();

   args.GetReturnValue().Set(Number::New(isolate, res ? res : countPending));
}

//-----------------------------------------------------------------
// fillSendArguments
//-----------------------------------------------------------------

void EmsClientWrapper::fillSendArguments(Local<Object>& obj, Isolate* isolate, Work* work)
{
   if (!work)
      return;

   if (has(obj, isolate, "to"))
   {
      Local<Value> subObj= obj->Get(v8::String::NewFromUtf8(isolate, "to"));

      if (!subObj->IsObject())
      {
         isolate->ThrowException(Exception::TypeError(v8::String::NewFromUtf8(isolate, "Expected 'to' value to be of type object")));
         return;
      }

      Local<Object> to= subObj->ToObject();

      if (has(to, isolate, "destination") && has(to, isolate, "isTopic"))
         work->message->setDestination(getString(to, isolate, "destination").c_str(), getInteger(to, isolate, "isTopic"));
   }

   // header params will be encapsulated in a separate object. need to iterate the key/value pairs and add them to the message
   // { header: { correlationID: "someVal" } }

   if (has(obj, isolate, "header"))
   {
      Local<Value> subObj= obj->Get(v8::String::NewFromUtf8(isolate, "header"));

      if (!subObj->IsObject())
      {
         isolate->ThrowException(Exception::TypeError(v8::String::NewFromUtf8(isolate, "Expected 'header' value to be of type object")));
         return;
      }

      Local<Object> header= subObj->ToObject();

      if (has(header, isolate, "correlationID")) work->message->setCorrelationID(getString(header, isolate, "correlationID").c_str());
      if (has(header, isolate, "encoding")) work->message->setEncoding(getString(header, isolate, "encoding").c_str());
      if (has(header, isolate, "expiration")) work->message->setExpiration((long)getInteger(header, isolate, "expiration"));
      if (has(header, isolate, "messageID")) work->message->setMessageID(getString(header, isolate, "messageID").c_str());
      if (has(header, isolate, "priority")) work->message->setPriority(getInteger(header, isolate, "priority"));

      // replyTo in separate object: { destination: "some.topic", isTopic: true }

      if (has(header, isolate, "replyTo"))
      {
         Local<Value> subObj= header->Get(v8::String::NewFromUtf8(isolate, "replyTo"));

         if (!subObj->IsObject())
         {
            isolate->ThrowException(Exception::TypeError(v8::String::NewFromUtf8(isolate, "Expected 'replyTo' value to be of type object")));
            return;
         }

         Local<Object> replyTo= subObj->ToObject();

         if (has(replyTo, isolate, "destination") && has(replyTo, isolate, "isTopic"))
            work->message->setReplyTo(getString(replyTo, isolate, "destination").c_str(), getInteger(replyTo, isolate, "isTopic"));
      }
   }

   // properties will be encapsulated in a separate object. need to iterate the key/value pairs and add them to the message
   // { properties: { someProp: "someVal", otherProp: 15 } }

   if (has(obj, isolate, "properties"))
   {
      Local<Value> subObj= obj->Get(v8::String::NewFromUtf8(isolate, "properties"));

      if (!subObj->IsObject())
      {
         isolate->ThrowException(Exception::TypeError(v8::String::NewFromUtf8(isolate, "Expected 'properties' value to be of type object")));
         return;
      }

      Local<Object> properties= subObj->ToObject();
      Local<Array> propertyNames = properties->GetOwnPropertyNames();

      for (int i = 0; i < (int)propertyNames->Length(); ++i)
      {
         Local<Value> key = propertyNames->Get(i);
         Local<Value> value = properties->Get(key);

         if (!key->IsString()) continue;  // ???

         const std::string name= *v8::String::Utf8Value(key->ToString());

         if (value->IsString()) 
         {
            const std::string stringVal= *v8::String::Utf8Value(value->ToString());
            work->message->setProperty(name.c_str(), stringVal.c_str());
         }
         else if (value->IsInt32())
         {
            work->message->setProperty(name.c_str(), (int)value->ToInteger()->Value());
         }
         else if (value->IsBoolean())
         {
            work->message->setProperty(name.c_str(), value->ToBoolean()->Value());
         }
         else if (value->IsNumber())
         {
            work->message->setProperty(name.c_str(), value->ToNumber()->Value());
         }
      }         
   }

}

//-----------------------------------------------------------------
// setMessageHandler
// set JS callback function for all future received messages
//-----------------------------------------------------------------

void EmsClientWrapper::setMessageHandler(const FunctionCallbackInfo<Value>& args)
{
  Isolate* isolate= args.GetIsolate();
  HandleScope scope(isolate);
  
  if (args.Length() < 1 || !args[0]->IsFunction())
  {
    isolate->ThrowException(Exception::TypeError(v8::String::NewFromUtf8(isolate, "Expected function argument")));
    return;
  }

  // assign callback to global persisten variable

  Local<Function> callback= Local<Function>::Cast(args[0]);
  onMessageCallback.Reset(isolate, callback); // differs from example
  
  callbackSet= yes;
}

//-----------------------------------------------------------------
// Object Helpers
//-----------------------------------------------------------------

std::string EmsClientWrapper::getString(Local<Object>& object, Isolate* isolate, const char* propertyName)
{
   HandleScope scope(isolate);
   Local<Value> c= object->Get(v8::String::NewFromUtf8(isolate, propertyName));
  
   if (c->IsString())
      return *v8::String::Utf8Value(c->ToString());

   return std::string();
}

int EmsClientWrapper::getInteger(Local<Object>& object, Isolate* isolate, const char* propertyName)
{
   HandleScope scope(isolate);
   Local<Value> c= object->Get(v8::String::NewFromUtf8(isolate, propertyName));
  
   if (c->IsInt32() || c->IsBoolean() || c->IsNumber())
      return c->ToInteger()->Value();

   return 0;
}

double EmsClientWrapper::getNumber(Local<Object>& object, Isolate* isolate, const char* propertyName)
{
   HandleScope scope(isolate);
   Local<Value> c= object->Get(v8::String::NewFromUtf8(isolate, propertyName));
  
   if (c->IsInt32() || c->IsNumber())
      return c->ToNumber()->Value();

   return 0.0;
}

int EmsClientWrapper::has(Local<Object>& object, Isolate* isolate, const char* propertyName)
{
   return object->Has(v8::String::NewFromUtf8(isolate, propertyName));
}

//-----------------------------------------------------------------
// initializer
//-----------------------------------------------------------------

void init(Handle<Object> exports)
{
  EmsClientWrapper::Init(exports);
}

//-----------------------------------------------------------------
// Node.JS module registration
//-----------------------------------------------------------------

NODE_MODULE(emsclient, init)
