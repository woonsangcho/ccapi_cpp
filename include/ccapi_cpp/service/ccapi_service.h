#ifndef INCLUDE_CCAPI_CPP_SERVICE_CCAPI_SERVICE_H_
#define INCLUDE_CCAPI_CPP_SERVICE_CCAPI_SERVICE_H_
#include "ccapi_cpp/ccapi_request.h"
#include "ccapi_cpp/ccapi_subscription.h"
namespace ccapi {
class Service {
 public:
  typedef wspp::lib::shared_ptr<ServiceContext> ServiceContextPtr;
  Service(std::function<void(Event& event)> eventHandler, SessionOptions sessionOptions,
                                      SessionConfigs sessionConfigs, ServiceContextPtr serviceContextPtr)
      :
        eventHandler(eventHandler),
        sessionOptions(sessionOptions),
        sessionConfigs(sessionConfigs), serviceContextPtr(serviceContextPtr) {
  }
  virtual ~Service() {
  }
  void setEventHandler(const std::function<void(Event& event)>& eventHandler) {
    this->eventHandler = eventHandler;
  }
  virtual void stop() = 0;
  virtual void subscribe(const std::vector<Subscription>& subscriptionList) = 0;
  virtual std::shared_ptr<std::future<void> > sendRequest(const Request& request, const bool useFuture, const TimePoint& now) {
    return std::shared_ptr<std::future<void> >(nullptr);
  }

 protected:
  std::string name;
  std::string baseUrl;
  std::string baseUrlRest;
  std::function<void(Event& event)> eventHandler;
  SessionOptions sessionOptions;
  SessionConfigs sessionConfigs;
  ServiceContextPtr serviceContextPtr;
};
} /* namespace ccapi */
#endif  // INCLUDE_CCAPI_CPP_SERVICE_CCAPI_SERVICE_H_
