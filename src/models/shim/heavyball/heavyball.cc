#include "heavyball.h"
#include "emane/events/pathlossevent.h"
#include "emane/events/pathlosseventformatter.h"
#include "emane/mactypes.h"

// controls
#include "emane/controls/frequencyofinterestcontrolmessage.h"
#include "emane/controls/flowcontrolcontrolmessage.h"
#include "emane/controls/serializedcontrolmessage.h"

#include "emane/controls/frequencycontrolmessage.h"
#include "emane/controls/frequencycontrolmessageformatter.h"
#include "emane/controls/receivepropertiescontrolmessage.h"
#include "emane/controls/receivepropertiescontrolmessageformatter.h"
#include "emane/controls/timestampcontrolmessage.h"
#include "emane/controls/transmittercontrolmessage.h"


#include "emane/utils/conversionutils.h"
// TDMA
#include "emane/models/tdma/messagecomponent.h"
#include "../../mac/tdma/basemodelmessage.h"
#include "../../mac/tdma/eventscheduler/eventscheduler.h"


#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

// #include "emane/utils/pathlossesholder.h"

namespace EMANE {
  namespace Models {
    namespace HeavyBall {
      void HBShimLayer::initialize(Registrar & registrar) {
        auto & configRegistrar = registrar.configurationRegistrar();

        // FIXME error if beta is not provided, or default to 0.0
        configRegistrar.registerNumeric<float>("beta",
                                               ConfigurationProperties::DEFAULT,
                                               {0.0},
                                               "beta in HeavyBall.");
        auto & eventRegistrar = registrar.eventRegistrar();
        eventRegistrar.registerEvent(Events::PathlossEvent::IDENTIFIER);
      }

      void HBShimLayer::configure(const ConfigurationUpdate & update) {
        for(const auto & item : update) {
          if(item.first == "beta") {
            float beta = item.second[0].asFloat();
            m_beta = beta;
          }
        }
      }

      void HBShimLayer::start() {
        m_pQueueManager->start();
        m_pScheduler->start();
      }

      void HBShimLayer::stop() {
        m_pQueueManager->stop();
        m_pScheduler->stop();
      }

      void HBShimLayer::postStart() {
        m_pQueueManager->postStart();
        m_pScheduler->postStart();
      }

      void HBShimLayer::destroy() throw () {
        m_pQueueManager->destroy();
        m_pScheduler->destroy();
      }

      void HBShimLayer::processUpstreamControl(const ControlMessages &) {}

      void HBShimLayer::processDownstreamControl(const ControlMessages &) {}

      void HBShimLayer::processUpstreamPacket(UpstreamPacket & pkt,
                                              const ControlMessages & msgs) {}

      void HBShimLayer::processDownstreamPacket(DownstreamPacket &, const ControlMessages &) {}

      void HBShimLayer::processEvent(const EventId & eventId,
                        const Serialization & serialization) {
        // pathloss event
        if (eventId == Events::PathlossEvent::IDENTIFIER) {
          Events::PathlossEvent pathlossEvent{serialization};

          m_pathlossesHolder = pathlossEvent.getPathlosses();
          m_pathlossInitialized = true;

          // FIXME members
          // pPropagationModelAlgorithm_->update(pathlossEvent.getPathlosses());
          // eventTablePublisher_.update(pathlossEvent.getPathlosses());
        }
        m_pScheduler->processEvent(eventId, serialization);
      }

      void HBShimLayer::processTimedEvent(TimerEventId,
                                          const TimePoint &,
                                          const TimePoint &,
                                          const TimePoint &,
                                          const void *) {}


      // TODO add into header file
      void HBShimLayer::processSchedulerPacket(DownstreamPacket & pkt) {
        m_pQueueManager->enqueue(4,std::move(pkt));
      }
      void HBShimLayer::processSchedulerControl(const ControlMessages & msgs) {
        // m_pRadioModel->sendDownstreamControl(msgs);
        this->sendDownstreamControl(msgs);
      }
      // FIXME is this useful?
      EMANE::Models::TDMA::QueueInfos HBShimLayer::getPacketQueueInfo() const {
        return m_pQueueManager->getPacketQueueInfo();
      }

      void HBShimLayer::sendUpstreamPacket(UpstreamPacket & pkt,
                              const ControlMessages & msgs) {}
      void HBShimLayer::sendUpstreamControl(const ControlMessages & msgs) {}
      void HBShimLayer::sendDownstreamPacket(DownstreamPacket & pkt,
                                const ControlMessages & msgs) {
        size_t bytesAvailable =
          (m_slotDuration.count() - m_slotOverhead.count())
          / 1000000.0 * m_pendingTxSlotInfo.u64DataRatebps_ / 8.0;
        NEMId dst = getDstByMaxWeight();
        auto entry = m_pQueueManager->dequeue(m_pendingTxSlotInfo.u8QueueId_,
                                              bytesAvailable,
                                              dst);
        TDMA::MessageComponents & components = std::get<0>(entry);
        size_t totalSize{std::get<1>(entry)};
        if (totalSize == 0) return;
        if(totalSize <= bytesAvailable) {
          float fSeconds{totalSize * 8.0f / m_pendingTxSlotInfo.u64DataRatebps_};
          Microseconds duration{std::chrono::duration_cast<Microseconds>
              (DoubleSeconds{fSeconds})};

          // rounding error corner case mitigation
          if(duration >= m_slotDuration) {
            duration = m_slotDuration - Microseconds{1};}

          NEMId dst{};
          size_t completedPackets{};

          // determine how many components represent completed
          // packets (no fragments remain) and whether to use a
          // unicast or broadcast nem address
          for(const auto & component : components) {
            completedPackets += !component.isMoreFragments();

            // if not set, set a destination
            if(!dst) {
              dst = component.getDestination();}
            else if(dst != NEM_BROADCAST_MAC_ADDRESS) {
              // if the destination is not broadcast, check to
              // see if it matches the destination of the
              // current component - if not, set the NEM
              // broadcast address as the dst
              if(dst != component.getDestination()) {
                dst = NEM_BROADCAST_MAC_ADDRESS;}}}

          // FIXME is this important?
          // if(bFlowControlEnable_ && completedPackets) {
          //   auto status = flowControlManager_.addToken(completedPackets);}

          // aggregationStatusPublisher_.update(components);

          TDMA::BaseModelMessage baseModelMessage{m_pendingTxSlotInfo.u64AbsoluteSlotIndex_,
              m_pendingTxSlotInfo.u64DataRatebps_,
              std::move(components)};

          Serialization serialization{baseModelMessage.serialize()};

          auto now = Clock::now();

          DownstreamPacket pkt({id_,dst,0,now},serialization.c_str(),serialization.size());

          pkt.prependLengthPrefixFraming(serialization.size());

          // m_pRadioModel->sendDownstreamPacket
          // FIXME this is implementation detail, different signature
          // this->sendDownstreamPacket
          //   (CommonMACHeader{REGISTERED_EMANE_MAC_TDMA,m_u64SequenceNumber++},
          //    pkt,
          //    {Controls::FrequencyControlMessage::create
          //        (m_u64BandwidthHz,
          //         {{m_pendingTxSlotInfo.u64FrequencyHz_,duration}}),
          //        Controls::TimeStampControlMessage::create(m_pendingTxSlotInfo.timePoint_),
          //        Controls::TransmitterControlMessage::create({{id_,m_pendingTxSlotInfo.dPowerdBm_}})});

          // slotStatusTablePublisher_.update(pendingTxSlotInfo_.u32RelativeIndex_,
          //                                  pendingTxSlotInfo_.u32RelativeFrameIndex_,
          //                                  pendingTxSlotInfo_.u32RelativeSlotIndex_,
          //                                  SlotStatusTablePublisher::Status::TX_GOOD,
          //                                  dSlotRemainingRatio);

          // neighborMetricManager_.updateNeighborTxMetric(dst,
          //                                               pendingTxSlotInfo_.u64DataRatebps_,
          //                                               now);
        }
      }

      void HBShimLayer::sendDownstreamControl(const ControlMessages & msgs) {}


      // private utility
      EMANE::NEMId HBShimLayer::getDstByMaxWeight() {
        auto qls = m_pQueueManager->getDestQueueLength(0);

        // some check
        for (auto it=qls.begin(); it!=qls.end(); ++it) {
          if (65535 == it->first && it->second > 2) return 65535;}

        EMANE::NEMId nemId{0};
        double maxScore = 0;

        if (m_pathlossInitialized) {
          std::string msg = "";
          // the saved pathloss events
          EMANE::Events::Pathlosses pe = m_pathlossesHolder;
          for (auto const& it: pe) {
            // some non-important msg construction
            if (msg != "") {
              msg.append(",");}
            // get which node is sending the event
            auto id = it.getNEMId();
            auto ql = qls.find(id);
            msg.append(std::to_string(id));
            if (ql == qls.end()) {
              msg.append(":0:0");
              continue;}

            // calculating weights?
            double weight = m_lastWeight[id] + ql->second - m_lastQueueLength[id]
              + m_beta * (m_lastWeight[id] - m_lastLastWeight[id]
                         + ql->second
                         + m_lastLastQueueLength[id]
                         - 2 * m_lastQueueLength[id]);

            // []+
            if (weight < 0) {
              weight = 0;}

            // momentum
            m_weightT[id] += m_lastWeight[id];

            m_lastLastWeight[id] = m_lastWeight[id];
            m_lastWeight[id] = weight;
            m_lastLastQueueLength[id] = m_lastQueueLength[id];
            m_lastQueueLength[id] = ql->second;

            // some more msg construction
            msg.append(":");
            msg.append(std::to_string(m_weightT[id]));
            msg.append(":");
            msg.append(std::to_string(ql->second));

            // todo: change 110 to txpower - noise
            double snr = EMANE::Utils::DB_TO_MILLIWATT(110-it.getForwardPathlossdB());
            // double score = log2(1.0 + snr) * ql->second;
            double score = log2(1.0 + snr) * weight;

            // Set the id for return, if the score is the
            // largest. This is inside the loop, thus done for all
            // pathloss events in the holder.
            if (score > maxScore) {
              nemId = id;
              maxScore = score;}
          }

          // create socket and ready for send data out.
          for (int i = 0; i < 10; i++) {
              m_weightT[i] = 0;}
          int sock_fd = -1;
          // #define MAXDATASIZE 1000
          // char buf[MAXDATASIZE];
          char buf[1000];
          int recvbytes, sendbytes, len;

          // This socket is created by client emane.
          std::string fifo = "/tmp/emane-mgen_fifo_node" + std::to_string(id_);
          int fd = open(fifo.c_str(), O_WRONLY);
          write(fd, msg.c_str(), msg.length() + 1);
          close(fd);
        }

        return nemId;
      }


      HeavyBallModel::HeavyBallModel(NEMId id,
                                     PlatformServiceProvider * pPlatformServiceProvider,
                                     RadioServiceProvider * pRadioServiceProvider)
      : HBShimLayer{id,
          pPlatformServiceProvider,
          pRadioServiceProvider,
          // FIXME NULL should be this
          new TDMA::EventScheduler{id,pPlatformServiceProvider,NULL},
          new TDMA::BasicQueueManager{id,pPlatformServiceProvider}} {}

    }
  }
}


// DECLARE_SHIM_LAYER(EMANE::Models::HeavyBall::HeavyBallModel);
// using HeavyBallEventSchedulerModel =
//   EMANE::Models::HeavyBall::HeavyBallModel
//   <EMANE::Models::TDMA::Scheduler, EMANE::Models::TDMA::BasicQueueManager>;

DECLARE_SHIM_LAYER(EMANE::Models::HeavyBall::HeavyBallModel);