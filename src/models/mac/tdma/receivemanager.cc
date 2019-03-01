/*
 * Copyright (c) 2015,2017 - Adjacent Link LLC, Bridgewater, New Jersey
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of Adjacent Link LLC nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "receivemanager.h"
#include "emane/utils/spectrumwindowutils.h"

EMANE::Models::TDMA::ReceiveManager::ReceiveManager(NEMId id,
                                                    DownstreamTransport * pDownstreamTransport,
                                                    LogServiceProvider * pLogService,
                                                    RadioServiceProvider * pRadioService,
                                                    Scheduler * pScheduler,
                                                    PacketStatusPublisher * pPacketStatusPublisher,
                                                    NeighborMetricManager * pNeighborMetricManager):
  id_{id},
  pDownstreamTransport_{pDownstreamTransport},
  pLogService_{pLogService},
  pRadioService_{pRadioService},
  pScheduler_{pScheduler},
  pPacketStatusPublisher_{pPacketStatusPublisher},
  pNeighborMetricManager_{pNeighborMetricManager},
  pendingInfo_{{},{{},{},{},{}},{},{},{},{},{},{}},
  u64PendingAbsoluteSlotIndex_{},
  distribution_{0.0, 1.0},
  bPromiscuousMode_{},
  fragmentCheckThreshold_{2},
  fragmentTimeoutThreshold_{5},
  neighborQlen_{}{}

void EMANE::Models::TDMA::ReceiveManager::setPromiscuousMode(bool bEnable)
{
  bPromiscuousMode_ = bEnable;
}

void EMANE::Models::TDMA::ReceiveManager::loadCurves(const std::string & sPCRFileName)
{
  porManager_.load(sPCRFileName);
}

void EMANE::Models::TDMA::ReceiveManager::setFragmentCheckThreshold(const std::chrono::seconds & threshold)
{
  fragmentCheckThreshold_ = threshold;
}

void EMANE::Models::TDMA::ReceiveManager::setFragmentTimeoutThreshold(const std::chrono::seconds & threshold)
{
  fragmentTimeoutThreshold_ = threshold;
}


bool
EMANE::Models::TDMA::ReceiveManager::enqueue(BaseModelMessage && baseModelMessage,
                                             const PacketInfo & pktInfo,
                                             size_t length,
                                             const TimePoint & startOfReception,
                                             const FrequencySegments & frequencySegments,
                                             const Microseconds & span,
                                             const TimePoint & beginTime,
                                             std::uint64_t u64PacketSequence)
{
  bool bReturn{};
  std::uint64_t u64AbsoluteSlotIndex{baseModelMessage.getAbsoluteSlotIndex()};

  if(!u64PendingAbsoluteSlotIndex_)
    {
      u64PendingAbsoluteSlotIndex_ = u64AbsoluteSlotIndex;

      pendingInfo_ = std::make_tuple(std::move(baseModelMessage),
                                     pktInfo,
                                     length,
                                     startOfReception,
                                     frequencySegments,
                                     span,
                                     beginTime,
                                     u64PacketSequence);
      bReturn = true;
    }
  else if(u64PendingAbsoluteSlotIndex_ < u64AbsoluteSlotIndex)
    {
      process(u64AbsoluteSlotIndex);

      u64PendingAbsoluteSlotIndex_ = u64AbsoluteSlotIndex;

      pendingInfo_ = std::make_tuple(std::move(baseModelMessage),
                                     pktInfo,
                                     length,
                                     startOfReception,
                                     frequencySegments,
                                     span,
                                     beginTime,
                                     u64PacketSequence);
      bReturn = true;
    }
  else if(u64PendingAbsoluteSlotIndex_ > u64AbsoluteSlotIndex)
    {
      LOGGER_VERBOSE_LOGGING(*pLogService_,
                             ERROR_LEVEL,
                             "MACI %03hu TDMA::ReceiveManager enqueue: pending slot: %zu greater than enqueue: %zu",
                             id_,
                             u64PendingAbsoluteSlotIndex_,
                             u64AbsoluteSlotIndex);

      u64PendingAbsoluteSlotIndex_ = u64AbsoluteSlotIndex;

      pendingInfo_ = std::make_tuple(std::move(baseModelMessage),
                                     pktInfo,
                                     length,
                                     startOfReception,
                                     frequencySegments,
                                     span,
                                     beginTime,
                                     u64PacketSequence);
      bReturn = true;

    }
  else
    {
      if(std::get<3>(pendingInfo_)  < startOfReception)
        {
          pendingInfo_ = std::make_tuple(std::move(baseModelMessage),
                                         pktInfo,
                                         length,
                                         startOfReception,
                                         frequencySegments,
                                         span,
                                         beginTime,
                                         u64PacketSequence);
        }
    }

  return bReturn;
}

void
EMANE::Models::TDMA::ReceiveManager::process(std::uint64_t u64AbsoluteSlotIndex)
{
  auto now = Clock::now();

  if(u64PendingAbsoluteSlotIndex_ + 1 == u64AbsoluteSlotIndex)
    {
      u64PendingAbsoluteSlotIndex_ = 0;

      double dSINR{};
      double dNoiseFloordB{};

      BaseModelMessage & baseModelMessage = std::get<0>(pendingInfo_);
      PacketInfo pktInfo{std::get<1>(pendingInfo_)};
      size_t length{std::get<2>(pendingInfo_)};
      TimePoint & startOfReception = std::get<3>(pendingInfo_);
      FrequencySegments & frequencySegments = std::get<4>(pendingInfo_);
      Microseconds & span = std::get<5>(pendingInfo_);
      std::uint64_t u64SequenceNumber{std::get<7>(pendingInfo_)};

      auto & frequencySegment = *frequencySegments.begin();

      try
        {
          auto window = pRadioService_->spectrumService().request(frequencySegment.getFrequencyHz(),
                                                                  span,
                                                                  startOfReception);


          bool bSignalInNoise{};

          LOGGER_VERBOSE_LOGGING(*pLogService_,
                                 DEBUG_LEVEL,
                                 "MACI %03hu TDMA::ReceiveManager RxPowerdBm-before %lf",
                                 id_,
                                 frequencySegment.getRxPowerdBm());

          std::tie(dNoiseFloordB,bSignalInNoise) =
            Utils::maxBinNoiseFloor(window,frequencySegment.getRxPowerdBm());

          dSINR = frequencySegment.getRxPowerdBm() - dNoiseFloordB;

          LOGGER_VERBOSE_LOGGING(*pLogService_,
                                 DEBUG_LEVEL,
                                 "MACI %03hu TDMA::ReceiveManager RxPowerdBm-after %lf",
                                 id_,
                                 frequencySegment.getRxPowerdBm());

          LOGGER_VERBOSE_LOGGING(*pLogService_,
                                 DEBUG_LEVEL,
                                 "MACI %03hu TDMA::ReceiveManager upstream EOR processing:"
                                 " src %hu, dst %hu, max noise %lf, signal in noise %s, SINR %lf",
                                 id_,
                                 pktInfo.getSource(),
                                 pktInfo.getDestination(),
                                 dNoiseFloordB,
                                 bSignalInNoise ? "yes" : "no",
                                 dSINR);
        }
      catch(SpectrumServiceException & exp)
        {
          pPacketStatusPublisher_->inbound(pktInfo.getSource(),
                                           baseModelMessage.getMessages(),
                                           PacketStatusPublisher::InboundAction::DROP_SPECTRUM_SERVICE);


          LOGGER_VERBOSE_LOGGING(*pLogService_,
                                 ERROR_LEVEL,
                                 "MACI %03hu TDMA::ReceiveManager upstream EOR processing: src %hu,"
                                 " dst %hu, sor %ju, span %ju spectrum service request error: %s",
                                 id_,
                                 pktInfo.getSource(),
                                 pktInfo.getDestination(),
                                 std::chrono::duration_cast<Microseconds>(startOfReception.time_since_epoch()).count(),
                                 span.count(),
                                 exp.what());

          return;
        }


      // check sinr
      float fPOR = porManager_.getPOR(baseModelMessage.getDataRate(),dSINR,length);

      LOGGER_VERBOSE_LOGGING(*pLogService_,
                             DEBUG_LEVEL,
                             "MACI %03hu TDMA::ReceiveManager upstream EOR processing: src %hu,"
                             " dst %hu, datarate: %ju sinr: %lf length: %lu, por: %f",
                             id_,
                             pktInfo.getSource(),
                             pktInfo.getDestination(),
                             baseModelMessage.getDataRate(),
                             dSINR,
                             length,
                             fPOR);

      // get random value [0.0, 1.0]
      float fRandom{distribution_()};

      if(fPOR < fRandom)
        {
          pPacketStatusPublisher_->inbound(pktInfo.getSource(),
                                           baseModelMessage.getMessages(),
                                           PacketStatusPublisher::InboundAction::DROP_SINR);

          LOGGER_VERBOSE_LOGGING(*pLogService_,
                                 DEBUG_LEVEL,
                                 "MACI %03hu TDMA::ReceiveManager upstream EOR processing: src %hu, dst %hu, "
                                 "rxpwr %3.2f dBm, drop",
                                 id_,
                                 pktInfo.getSource(),
                                 pktInfo.getDestination(),
                                 frequencySegment.getRxPowerdBm());

          return;
        }


      // update neighbor metrics
      pNeighborMetricManager_->updateNeighborRxMetric(pktInfo.getSource(),    // nbr (src)
                                                      u64SequenceNumber,      // sequence number
                                                      pktInfo.getUUID(),
                                                      dSINR,                  // sinr in dBm
                                                      dNoiseFloordB,          // noise floor in dB
                                                      startOfReception,       // rx time
                                                      frequencySegment.getDuration(), // duration
                                                      baseModelMessage.getDataRate()); // data rate bps

      for(const auto & message : baseModelMessage.getMessages())
        {
          NEMId dst{message.getDestination()};
          Priority priority{message.getPriority()};


          if(message.getType() == MessageComponent::Type::QUEUELENGTH) {
            std::map<std::pair<NEMId,NEMId>,size_t> tmpNeighborQlen;
            NEMId dst = 0;
            NEMId src = 0;
            std::size_t qlen = 0;
            NEMId srcId = 0;
            bool hasDst = false;
            bool hasSrc = false;
            bool hasId = false;
            const auto & data = message.getData();
            for (auto c : data){
              LOGGER_VERBOSE_LOGGING(*pLogService_,
                                    DEBUG_LEVEL,
                                    "MACI %03hu TDMA:: %hu @#@#@#@ %c",
                                    id_,
                                    c,
                                    c);
              // 1#1-2:3,2-3:4
              if (c == ':') {
                hasDst = true;
              } else if (c == '#'){
                // tmpNeighborQlen.insert(std::pair<std::pair<NEMId,NEMId>,size_t>(srcId, 0));
                hasId = true;
              } else if (c == '-') {
                hasSrc = true;
              } else if (c == ',') {
                hasDst = false;
                tmpNeighborQlen.insert(std::pair<std::pair<NEMId,NEMId>,size_t>(std::make_pair(src, dst), qlen));
                qlen = 0;
                dst = 0;
                src = 0;
              } else if (hasId){
                // qlen = qlen * 10 + c - 48;
                if (hasSrc && !hasDst) {
                  src = src * 10 + c - 48;
                } else if (hasDst) {
                  qlen = qlen * 10 + c - 48;
                } else {
                  dst = dst * 10 + c - 48;
                }
              } else {
                srcId = srcId * 10 + c - 48;
              }
            }
            tmpNeighborQlen.insert(std::pair<std::pair<NEMId,NEMId>,size_t>(std::make_pair(src, dst), qlen));
            neighborQlen_.insert(std::make_pair(srcId, tmpNeighborQlen));
            LOGGER_VERBOSE_LOGGING(*pLogService_,
                                         DEBUG_LEVEL,
                                         "MACI %03hu TDMA:: recived length %hu @#@#@#@",
                                         id_,
                                         data.size());
            
            
            continue;

          }

          if(bPromiscuousMode_ ||
             (dst == id_) ||
             (dst == NEM_BROADCAST_MAC_ADDRESS))
            {
              const auto & data = message.getData();

              if(message.isFragment())
                {
                  LOGGER_VERBOSE_LOGGING(*pLogService_,
                                         DEBUG_LEVEL,
                                         "MACI %03hu TDMA::ReceiveManager upstream EOR processing:"
                                         " src %hu, dst %hu, findex: %zu foffset: %zu fbytes: %zu"
                                         " fmore: %s",
                                         id_,
                                         pktInfo.getSource(),
                                         pktInfo.getDestination(),
                                         message.getFragmentIndex(),
                                         message.getFragmentOffset(),
                                         data.size(),
                                         message.isMoreFragments() ? "yes" : "no");


                  auto key = std::make_tuple(pktInfo.getSource(),
                                             priority,
                                             message.getFragmentSequence());

                  auto iter = fragmentStore_.find(key);

                  if(iter !=  fragmentStore_.end())
                    {
                      auto & indexSet = std::get<0>(iter->second);
                      auto & parts = std::get<1>(iter->second);
                      auto & lastFragmentTime = std::get<2>(iter->second);

                      if(indexSet.insert(message.getFragmentIndex()).second)
                        {
                          parts.insert(std::make_pair(message.getFragmentOffset(),message.getData()));

                          lastFragmentTime = now;

                          // check that all previous fragments have been received
                          if(indexSet.size() == message.getFragmentIndex() + 1)
                            {
                              if(!message.isMoreFragments())
                                {
                                  Utils::VectorIO vectorIO{};

                                  for(const auto & part : parts)
                                    {
                                      vectorIO.push_back(Utils::make_iovec(const_cast<std::uint8_t *>(&part.second[0]),
                                                                           part.second.size()));
                                    }

                                  UpstreamPacket pkt{{pktInfo.getSource(),
                                        dst,
                                        priority,
                                        pktInfo.getCreationTime(),
                                        pktInfo.getUUID()},vectorIO};


                                  pPacketStatusPublisher_->inbound(pktInfo.getSource(),
                                                                   dst,
                                                                   priority,
                                                                   pkt.length(),
                                                                   PacketStatusPublisher::InboundAction::ACCEPT_GOOD);


                                  PacketMetaInfo packetMetaInfo{pktInfo.getSource(),
                                      u64AbsoluteSlotIndex-1,
                                      frequencySegment.getRxPowerdBm(),
                                      dSINR,
                                      baseModelMessage.getDataRate()};

                                  if(message.getType() == MessageComponent::Type::DATA)
                                    {
                                      pDownstreamTransport_->sendUpstreamPacket(pkt);

                                      pScheduler_->processPacketMetaInfo(packetMetaInfo);
                                    }
                                  else
                                    {
                                      pScheduler_->processSchedulerPacket(pkt,packetMetaInfo);
                                    }


                                  fragmentStore_.erase(iter);
                                }
                            }
                          else
                            {
                              // missing a fragment - record all bytes received and discontinue assembly
                              size_t totalBytes{message.getData().size()};

                              for(const auto & part : parts)
                                {
                                  totalBytes += part.second.size();
                                }

                              pPacketStatusPublisher_->inbound(pktInfo.getSource(),
                                                               dst,
                                                               priority,
                                                               totalBytes,
                                                               PacketStatusPublisher::InboundAction::DROP_MISS_FRAGMENT);

                              // fragment was not received, abandon reassembly
                              fragmentStore_.erase(iter);
                            }
                        }
                    }
                  else
                    {
                      // if the first fragment receieved is not index 0, fragments
                      // were lost, so don't bother trying to reassemble
                      if(!message.getFragmentIndex())
                        {
                          fragmentStore_.insert(std::make_pair(key,
                                                               std::make_tuple(std::set<size_t>{message.getFragmentIndex()},
                                                                               FragmentParts{{message.getFragmentOffset(),
                                                                                     message.getData()}},
                                                                               now,
                                                                               dst,
                                                                               priority)));
                        }
                      else
                        {
                          pPacketStatusPublisher_->inbound(pktInfo.getSource(),
                                                           message,
                                                           PacketStatusPublisher::InboundAction::DROP_MISS_FRAGMENT);

                        }
                    }
                }
              else
                {
                  LOGGER_VERBOSE_LOGGING(*pLogService_,
                                         DEBUG_LEVEL,
                                         "MACI %03hu TDMA::ReceiveManager upstream EOR processing:"
                                         " src %hu, dst %hu, forward upstream",
                                         id_,
                                         pktInfo.getSource(),
                                         pktInfo.getDestination());


                  auto data = message.getData();

                  UpstreamPacket pkt{{pktInfo.getSource(),
                        dst,
                        priority,
                        pktInfo.getCreationTime(),
                        pktInfo.getUUID()},&data[0],data.size()};


                  pPacketStatusPublisher_->inbound(pktInfo.getSource(),
                                                   message,
                                                   PacketStatusPublisher::InboundAction::ACCEPT_GOOD);

                  PacketMetaInfo packetMetaInfo{pktInfo.getSource(),
                      u64AbsoluteSlotIndex-1,
                      frequencySegment.getRxPowerdBm(),
                      dSINR,
                      baseModelMessage.getDataRate()};

                  if(message.getType() == MessageComponent::Type::DATA)
                    {
                      pDownstreamTransport_->sendUpstreamPacket(pkt);

                      pScheduler_->processPacketMetaInfo(packetMetaInfo);
                    }
                  else
                    {
                      pScheduler_->processSchedulerPacket(pkt,packetMetaInfo);
                    }
                }
            }
          else
            {
              pPacketStatusPublisher_->inbound(pktInfo.getSource(),
                                               message,
                                               PacketStatusPublisher::InboundAction::DROP_DESTINATION_MAC);
            }
        }
    }

  // check to see if there are fragment assemblies to abandon
  if(lastFragmentCheckTime_ + fragmentCheckThreshold_ <= now)
    {
      for(auto iter = fragmentStore_.begin(); iter != fragmentStore_.end();)
        {
          auto & parts = std::get<1>(iter->second);
          auto & lastFragmentTime  = std::get<2>(iter->second);
          auto & dst  = std::get<3>(iter->second);
          auto & priority = std::get<4>(iter->second);

          if(lastFragmentTime + fragmentTimeoutThreshold_ <= now)
            {
              size_t totalBytes{};

              for(const auto & part : parts)
                {
                  totalBytes += part.second.size();
                }

              pPacketStatusPublisher_->inbound(std::get<0>(iter->first),
                                               dst,
                                               priority,
                                               totalBytes,
                                               PacketStatusPublisher::InboundAction::DROP_MISS_FRAGMENT);

              fragmentStore_.erase(iter++);
            }
          else
            {
              ++iter;
            }
        }

      lastFragmentCheckTime_ = now;
    }
}
