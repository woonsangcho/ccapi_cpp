#ifndef INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_HUOBI_H_
#define INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_HUOBI_H_
#ifdef CCAPI_ENABLE_SERVICE_MARKET_DATA
#ifdef CCAPI_ENABLE_EXCHANGE_HUOBI
#include "ccapi_cpp/service/ccapi_market_data_service.h"
#include <regex>
namespace ccapi {
class MarketDataServiceHuobi CCAPI_FINAL : public MarketDataService {
 public:
  MarketDataServiceHuobi(std::function<void(Event& event)> wsEventHandler, SessionOptions sessionOptions, SessionConfigs sessionConfigs, std::shared_ptr<ServiceContext> serviceContextPtr): MarketDataService(wsEventHandler, sessionOptions, sessionConfigs, serviceContextPtr) {
    this->name = CCAPI_EXCHANGE_NAME_HUOBI;
    this->baseUrl = sessionConfigs.getUrlWebsocketBase().at(this->name);
    ErrorCode ec = this->inflater.init(false, 31);
    if (ec) {
      CCAPI_LOGGER_FATAL(ec.message());
    }
  }

 private:
  std::vector<std::string> createRequestStringList(const WsConnection& wsConnection) override {
    std::vector<std::string> requestStringList;
    for (const auto & subscriptionListByChannelIdSymbolId : this->subscriptionListByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id)) {
      auto channelId = subscriptionListByChannelIdSymbolId.first;
      for (auto & subscriptionListByInstrument : subscriptionListByChannelIdSymbolId.second) {
        rj::Document document;
        document.SetObject();
        rj::Document::AllocatorType& allocator = document.GetAllocator();
        auto symbolId = subscriptionListByInstrument.first;
        if (channelId.rfind(CCAPI_WEBSOCKET_HUOBI_CHANNEL_MARKET_DEPTH, 0) == 0) {
          this->l2UpdateIsReplaceByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = true;
        }
        std::string exchangeSubscriptionId(CCAPI_WEBSOCKET_HUOBI_CHANNEL_MARKET_DEPTH);
        std::string toReplace("$symbol");
        exchangeSubscriptionId.replace(exchangeSubscriptionId.find(toReplace), toReplace.length(), symbolId);
        document.AddMember("sub", rj::Value(exchangeSubscriptionId.c_str(), allocator).Move(), allocator);
        rj::StringBuffer stringBuffer;
        rj::Writer<rj::StringBuffer> writer(stringBuffer);
        document.Accept(writer);
        std::string requestString = stringBuffer.GetString();
        requestStringList.push_back(std::move(requestString));
        this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_CHANNEL_ID] = channelId;
        this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_SYMBOL_ID] = symbolId;
        CCAPI_LOGGER_TRACE("this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap = "+toString(this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap));
      }
    }
    return requestStringList;
  }
  std::string getInstrumentGroup(const Subscription& subscription) override {
    auto url = this->baseUrl;
    auto field = subscription.getField();
    if (field == CCAPI_TRADE || field == CCAPI_MARKET_DEPTH) {
      url += "/ws";
    }
    return url + "|" + field + "|" + subscription.getSerializedOptions();
  }
  std::vector<MarketDataMessage> processTextMessage(wspp::connection_hdl hdl, const std::string& textMessage, const TimePoint& timeReceived) override {
    WsConnection& wsConnection = this->getWsConnectionFromConnectionPtr(this->serviceContextPtr->tlsClientPtr->get_con_from_hdl(hdl));
    rj::Document document;
    std::string quotedTextMessage = std::regex_replace(textMessage, std::regex("(\\[|,|\":)(-?\\d+\\.?\\d*[Ee]?-?\\d*)"), "$1\"$2\"");
    CCAPI_LOGGER_TRACE("quotedTextMessage = "+quotedTextMessage);
    document.Parse(quotedTextMessage.c_str());
    std::vector<MarketDataMessage> wsMessageList;
    if (document.IsObject() && document.HasMember("ch") && document.HasMember("tick")) {
      MarketDataMessage wsMessage;
      std::string exchangeSubscriptionId = document["ch"].GetString();
      std::string channelId = this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_CHANNEL_ID];
      std::string symbolId = this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_SYMBOL_ID];
      auto optionMap = this->optionMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId];
      CCAPI_LOGGER_TRACE("exchangeSubscriptionId = "+exchangeSubscriptionId);
      CCAPI_LOGGER_TRACE("channel = "+channelId);
      wsMessage.type = MarketDataMessage::Type::MARKET_DATA_EVENTS;
      if (std::regex_search(channelId, std::regex(CCAPI_WEBSOCKET_HUOBI_CHANNEL_TRADE_DETAIL_REGEX))) {
        // TODO(cryptochassis): implement
      } else if (std::regex_search(channelId, std::regex(CCAPI_WEBSOCKET_HUOBI_CHANNEL_MARKET_DEPTH_REGEX))) {
        CCAPI_LOGGER_TRACE("it is snapshot");
        if (this->processedInitialSnapshotByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]) {
          wsMessage.recapType = MarketDataMessage::RecapType::NONE;
        } else {
          wsMessage.recapType = MarketDataMessage::RecapType::SOLICITED;
        }
        const rj::Value& data = document["tick"];
        std::string ts = data["ts"].GetString();
        ts.insert(ts.size() - 3, ".");
        wsMessage.tp = UtilTime::makeTimePoint(UtilTime::divide(ts));
        CCAPI_LOGGER_TRACE("wsMessage.tp = " + toString(wsMessage.tp));
        wsMessage.exchangeSubscriptionId = exchangeSubscriptionId;
        int bidIndex = 0;
        int maxMarketDepth = std::stoi(optionMap.at(CCAPI_MARKET_DEPTH_MAX));
        for (const auto& x : data["bids"].GetArray()) {
          if (bidIndex >= maxMarketDepth) {
            break;
          }
          MarketDataMessage::TypeForDataPoint dataPoint;
          dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(x[0].GetString())});
          dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(x[1].GetString())});
          wsMessage.data[MarketDataMessage::DataType::BID].push_back(std::move(dataPoint));
          ++bidIndex;
        }
        int askIndex = 0;
        for (const auto& x : data["asks"].GetArray()) {
          if (askIndex >= maxMarketDepth) {
            break;
          }
          MarketDataMessage::TypeForDataPoint dataPoint;
          dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(x[0].GetString())});
          dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(x[1].GetString())});
          wsMessage.data[MarketDataMessage::DataType::ASK].push_back(std::move(dataPoint));
          ++askIndex;
        }
        wsMessageList.push_back(std::move(wsMessage));
      }
    } else if (document.IsObject() && document.HasMember("ping")) {
      std::string payload("{\"pong\":");
      payload += document["ping"].GetString();
      payload += "}";
      ErrorCode ec;
      this->send(hdl, payload, wspp::frame::opcode::text, ec);
      if (ec) {
        CCAPI_LOGGER_ERROR(ec.message());
        // TODO(cryptochassis): implement
      }
    } else if (document.IsObject() && document.HasMember("status") && document.HasMember("subbed")) {
    }
    return wsMessageList;
  }
};
} /* namespace ccapi */
#endif
#endif
#endif  // INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_HUOBI_H_
