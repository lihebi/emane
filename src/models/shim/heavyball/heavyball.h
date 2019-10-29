#ifndef HEAVYBALL_H
#define HEAVYBALL_H

#include "emane/shimlayerimpl.h"
#include "emane/types.h"
#include "emane/transmitter.h"
#include "emane/frequencysegment.h"

// pathloss
#include "emane/events/pathloss.h"


// TDMA model header files
#include "emane/models/tdma/types.h"
#include "emane/models/tdma/queue.h"
#include "emane/models/tdma/queuemanager.h"
#include "emane/models/tdma/basicqueuemanager.h"

// FIXME fix to first queue?
// #include "../../mac/tdma/priority.h"

#include <map>


namespace EMANE {
  namespace Models {
    namespace HeavyBall {
      // TODO use these queue and queue manager
      class HBQueue : TDMA::Queue {
      public:
        // FIXME remove original modification that adds these methods
        std::map<std::uint64_t,size_t> getDestQueueLength() {
          std::map<std::uint64_t,size_t> destQueueLength{};
          for (auto it=destQueue_.begin(); it!=destQueue_.end(); ++it) {
            destQueueLength.insert(std::make_pair(it->first,it->second.size()));}
          return destQueueLength;
        }
      };

      class HBQueueManager : TDMA::BasicQueueManager {
      public:
        std::map<std::uint64_t,size_t> getDestQueueLength(int priority) {
          return pImpl_->queues_[priority].getDestQueueLength();
        }
      }


      class HBShimLayer : public ShimLayerImplementor {
      public:
        HBShimLayer(MENId id,
                    PlatformServiceProvider * pPlatformService,
                    RadioServiceProvider * pRadioService,
                    Scheduler * pScheduler,
                    QueueManager * pQueueManager);
        ~HBShimLayer();
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
                                const ControlMessages & msgs = empty) override;
        void sendUpstreamControl(const ControlMessages & msgs) override;
        void sendDownstreamPacket(DownstreamPacket & pkt,
                                  const ControlMessages & msgs = empty) override;
        void sendDownstreamControl(const ControlMessages & msgs) override;


      private:
        EMANE::NEMId getDstByMaxWeight();
        std::unique_ptr<QueueManager> m_pQueueManager;
        std::unique_ptr<Scheduler> m_pScheduler;

        Microseconds m_slotDuration;
        Microseconds m_slotOverhead;
        TxSlotInfo m_pendingTxSlotInfo;

        std::uint64_t m_lastQueueLength[10] = 0;
        std::uint64_t m_lastLastQueueLength[10] = 0;
        double m_lastWeight[10] = 0;
        double m_lastLastWeight[10] = 0;
        double m_weightT[10] = 0;
        float m_beta = 0.0;

        Events::Pathlosses m_pathlossesHolder = {};
        bool m_pathlossInitialized = false;
      };
    }
  }
}

#endif /* HEAVYBALL_H */
