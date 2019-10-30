#ifndef HEAVYBALL_H
#define HEAVYBALL_H

#include "emane/shimlayerimpl.h"
#include "emane/maclayerimpl.h"
#include "emane/types.h"
#include "emane/transmitter.h"
#include "emane/frequencysegment.h"

// pathloss
#include "emane/events/pathloss.h"


// TDMA model header files
#include "emane/models/tdma/types.h"
#include "emane/models/tdma/queuemanager.h"
#include "emane/models/tdma/scheduler.h"
#include "emane/models/tdma/basicqueuemanager.h"

// FIXME fix to first queue?
// #include "../../mac/tdma/priority.h"
#include "../../mac/tdma/queue.h"
#include "../../mac/tdma/eventscheduler/eventscheduler.h"
#include "../../mac/tdma/queuestatuspublisher.h"


#include <map>


namespace EMANE {
  namespace Models {
    namespace HeavyBall {

      // TODO use these queue and queue manager
      class HBQueue : public TDMA::Queue {
      public:
        // FIXME remove original modification that adds these methods
        std::map<std::uint64_t,size_t> getDestQueueLength() {
          std::map<std::uint64_t,size_t> destQueueLength{};
          for (auto it=destQueue_.begin(); it!=destQueue_.end(); ++it) {
            destQueueLength.insert(std::make_pair(it->first,it->second.size()));}
          return destQueueLength;
        }
      private:
        class MetaInfo
        {
        public:
          size_t index_{};
          size_t offset_{};
        };
        using PacketQueue = std::map<std::uint64_t,
                                     std::pair<DownstreamPacket *,MetaInfo *>>;
        std::map<NEMId,PacketQueue> destQueue_;
      };

      class HBQueueManager : public TDMA::BasicQueueManager {
      public:
        std::map<std::uint64_t,size_t> getDestQueueLength(int priority) {
          return pImpl_->queues_[priority].getDestQueueLength();
        }
      private:
        class Implementation {
        public:
          bool bAggregationEnable_{};
          bool bFragmentationEnable_{};
          bool bStrictDequeueEnable_{};
          double dAggregationSlotThreshold_{};
          // const int MAX_QUEUES = 5;
          // HBQueue queues_[MAX_QUEUES];
          HBQueue queues_[5];
          TDMA::QueueStatusPublisher queueStatusPublisher_;
        };
        class Implementation;
        std::unique_ptr<Implementation> pImpl_;
      };

      class HBShimLayer : public ShimLayerImplementor {
      public:
        HBShimLayer(NEMId id,
                    PlatformServiceProvider * pPlatformService,
                    RadioServiceProvider * pRadioService,
                    TDMA::Scheduler * pScheduler,
                    TDMA::BasicQueueManager * pQueueManager
                    // MACLayerImplementor * pRadioModel
                    )
          : ShimLayerImplementor(id, pPlatformService, pRadioService),
            m_pScheduler{pScheduler},
            m_pQueueManager{pQueueManager}
            // m_pRadioModel{pRadioModel}
        {}

        ~HBShimLayer() {}

        void initialize(Registrar & registrar) override;

        void configure(const ConfigurationUpdate & update) override;

        void start() override;

        void postStart() override;

        void stop() override;

        void destroy() throw() override;

        void processUpstreamControl(const ControlMessages & msgs) override;

        void processDownstreamControl(const ControlMessages & msgs) override;

        void processUpstreamPacket(UpstreamPacket & pkt,
                                   const ControlMessages & msgs) override;

        void processDownstreamPacket(DownstreamPacket & pkt,
                                     const ControlMessages & msgs) override;

        void processEvent(const EventId & eventId,
                          const Serialization & serialization) override;

        void processTimedEvent(TimerEventId eventId,
                               const TimePoint & expireTime,
                               const TimePoint & scheduleTime,
                               const TimePoint & fireTime,
                               const void * arg) override;

        void sendUpstreamPacket(UpstreamPacket & pkt,
                                const ControlMessages & msgs = empty);
        void sendUpstreamControl(const ControlMessages & msgs);
        void sendDownstreamPacket(DownstreamPacket & pkt,
                                  const ControlMessages & msgs = empty);
        void sendDownstreamControl(const ControlMessages & msgs);

        void processSchedulerPacket(DownstreamPacket & pkt);
        void processSchedulerControl(const ControlMessages & msgs);
        EMANE::Models::TDMA::QueueInfos getPacketQueueInfo() const;

        static const ControlMessages empty;


      private:
        EMANE::NEMId getDstByMaxWeight();
        std::unique_ptr<TDMA::Scheduler> m_pScheduler;
        std::unique_ptr<TDMA::BasicQueueManager> m_pQueueManager;
        // MACLayerImplementor * m_pRadioModel;

        Microseconds m_slotDuration;
        Microseconds m_slotOverhead;
        TDMA::TxSlotInfo m_pendingTxSlotInfo;
        std::uint64_t m_u64SequenceNumber = 0;
        std::uint64_t m_u64BandwidthHz = 0;

        std::uint64_t m_lastQueueLength[10] = {0};
        std::uint64_t m_lastLastQueueLength[10] = {0};
        double m_lastWeight[10] = {0};
        double m_lastLastWeight[10] = {0};
        double m_weightT[10] = {0};
        float m_beta = 0.0;

        Events::Pathlosses m_pathlossesHolder = {};
        bool m_pathlossInitialized = false;
      };

      class HeavyBallModel : public HBShimLayer {
      public:
        HeavyBallModel(NEMId id,
                       PlatformServiceProvider * pPlatformServiceProvider,
                       RadioServiceProvider * pRadioServiceProvider);
        ~HeavyBallModel(){}
      };
    }
  }
}

#endif /* HEAVYBALL_H */
