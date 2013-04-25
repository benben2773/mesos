/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gmock/gmock.h>

#include <mesos/executor.hpp>
#include <mesos/scheduler.hpp>

#include <process/gmock.hpp>
#include <process/pid.hpp>
#include <process/process.hpp>

#include "local/local.hpp"

#include "master/master.hpp"

#include "tests/mesos.hpp"

using namespace mesos;
using namespace mesos::internal;
using namespace mesos::internal::tests;

using mesos::internal::master::Master;

using process::Future;
using process::PID;

using std::string;
using std::map;
using std::vector;

using testing::_;
using testing::AtMost;
using testing::Eq;
using testing::Return;


TEST(ExceptionTest, DeactivateFrameworkOnAbort)
{
  PID<Master> master = local::launch(1, 2, 1 * Gigabyte,1 * Gigabyte,  false);

  MockScheduler sched;

  MesosSchedulerDriver driver(&sched, DEFAULT_FRAMEWORK_INFO, master);

  Future<Nothing> registered;
  EXPECT_CALL(sched, registered(&driver, _, _))
    .WillOnce(FutureSatisfy(&registered));

  EXPECT_CALL(sched, resourceOffers(&driver, _))
    .WillRepeatedly(Return());

  EXPECT_CALL(sched, offerRescinded(&driver, _))
    .Times(AtMost(1));

  driver.start();

  AWAIT_READY(registered);

  Future<DeactivateFrameworkMessage> deactivateFrameworkMessage =
    FUTURE_PROTOBUF(DeactivateFrameworkMessage(), _, _);

  ASSERT_EQ(DRIVER_ABORTED, driver.abort());

  AWAIT_READY(deactivateFrameworkMessage);

  driver.stop();
  local::shutdown();
}


TEST(ExceptionTest, DisallowSchedulerActionsOnAbort)
{
  PID<Master> master = local::launch(1, 2, 1 * Gigabyte, 1 * Gigabyte, false);

  MockScheduler sched;

  MesosSchedulerDriver driver(&sched, DEFAULT_FRAMEWORK_INFO, master);

  Future<Nothing> registered;
  EXPECT_CALL(sched, registered(&driver, _, _))
    .WillOnce(FutureSatisfy(&registered));

  EXPECT_CALL(sched, resourceOffers(&driver, _))
    .WillRepeatedly(Return());

  driver.start();

  AWAIT_READY(registered);

  EXPECT_CALL(sched, offerRescinded(&driver, _))
    .Times(AtMost(1));

  ASSERT_EQ(DRIVER_ABORTED, driver.abort());

  ASSERT_EQ(DRIVER_ABORTED, driver.reviveOffers());

  driver.stop();
  local::shutdown();
}


TEST(ExceptionTest, DisallowSchedulerCallbacksOnAbort)
{
  PID<Master> master = local::launch(1, 2, 1 * Gigabyte, 1 * Gigabyte, false);

  MockScheduler sched;

  MesosSchedulerDriver driver(&sched, DEFAULT_FRAMEWORK_INFO, master);

  EXPECT_CALL(sched, registered(&driver, _, _))
    .Times(1);

  Future<vector<Offer> > offers;
  EXPECT_CALL(sched, resourceOffers(&driver, _))
    .WillOnce(FutureArg<1>(&offers))
    .WillRepeatedly(Return());

  Future<process::Message> message =
    FUTURE_MESSAGE(Eq(FrameworkRegisteredMessage().GetTypeName()), _, _);

  driver.start();

  AWAIT_READY(offers);
  EXPECT_NE(0u, offers.get().size());

  // None of these callbacks should be invoked.
  EXPECT_CALL(sched, offerRescinded(&driver, _))
    .Times(0);

  EXPECT_CALL(sched, statusUpdate(&driver, _))
      .Times(0);

  EXPECT_CALL(sched, frameworkMessage(&driver, _, _, _))
      .Times(0);

  EXPECT_CALL(sched, slaveLost(&driver, _))
      .Times(0);

  EXPECT_CALL(sched, error(&driver, _))
      .Times(0);

  ASSERT_EQ(DRIVER_ABORTED, driver.abort());

  Future<RescindResourceOfferMessage> rescindMsg =
    FUTURE_PROTOBUF(RescindResourceOfferMessage(), _, _);

  // Simulate a message from master to the scheduler.
  RescindResourceOfferMessage rescindMessage;
  rescindMessage.mutable_offer_id()->MergeFrom(offers.get()[0].id());

  process::post(message.get().to, rescindMessage);

  AWAIT_READY(rescindMsg);

  Future<UnregisterFrameworkMessage> unregisterMsg =
    FUTURE_PROTOBUF(UnregisterFrameworkMessage(), _, _);

  driver.stop();

  //Ensures reception of RescindResourceOfferMessage.
  AWAIT_READY(unregisterMsg);

  local::shutdown();
}
