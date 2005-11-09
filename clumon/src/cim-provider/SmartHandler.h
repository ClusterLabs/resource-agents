#ifndef SmartHandler_h
#define SmartHandler_h

template<class cl>
class SmartHandler
{
 public:
  SmartHandler(cl &handler) : _handler(handler)
    {
      _handler.processing();
    }
  virtual ~SmartHandler(void)
    {
      _handler.complete();
    }
  
 private:
  cl& _handler;


};


#endif
