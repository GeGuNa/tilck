/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <random>

#include <gtest/gtest.h>
#include "mocks.h"
#include "kernel_init_funcs.h"

extern "C" {

   #include <tilck/kernel/kmalloc.h>
   #include <tilck/kernel/tasklet.h>
   #include "kernel/tasklet_int.h" // private header

   extern u32 tasklet_threads_count;

   void destroy_last_tasklet_thread(void)
   {
      assert(tasklet_threads_count > 0);

      const u32 tn = --tasklet_threads_count;
      struct tasklet_thread *t = tasklet_threads[tn];
      assert(t != NULL);

      safe_ringbuf_destory(&t->rb);
      kfree2(t->tasklets, sizeof(struct tasklet) * t->limit);
      kfree2(t, sizeof(struct tasklet_thread));
      bzero((void *)t, sizeof(*t));
      tasklet_threads[tn] = NULL;
   }
}

using namespace std;
using namespace testing;

class tasklet_test : public Test {

   void SetUp() override {
      init_kmalloc_for_tests();
      init_tasklets();
   }

   void TearDown() override {
      destroy_last_tasklet_thread();
   }
};

void simple_func1(void *p1)
{
   ASSERT_EQ(p1, TO_PTR(1234));
}

TEST_F(tasklet_test, essential)
{
   bool res = false;

   ASSERT_TRUE(enqueue_tasklet(0, &simple_func1, TO_PTR(1234)));
   ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });
   ASSERT_TRUE(res);
}


TEST_F(tasklet_test, base)
{
   const int max_tasklets = get_tasklet_runner_limit(0);
   bool res;

   for (int i = 0; i < max_tasklets; i++) {
      res = enqueue_tasklet(0, &simple_func1, TO_PTR(1234));
      ASSERT_TRUE(res);
   }

   res = enqueue_tasklet(0, &simple_func1, TO_PTR(1234));

   // There is no more space left, expecting the ADD failed.
   ASSERT_FALSE(res);

   for (int i = 0; i < max_tasklets; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });
      ASSERT_TRUE(res);
   }

   ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });

   // There are no more tasklets, expecting the RUN failed.
   ASSERT_FALSE(res);
}


TEST_F(tasklet_test, advanced)
{
   const int max_tasklets = get_tasklet_runner_limit(0);
   bool res;

   // Fill half of the buffer.
   for (int i = 0; i < max_tasklets/2; i++) {
      res = enqueue_tasklet(0, &simple_func1, TO_PTR(1234));
      ASSERT_TRUE(res);
   }

   // Consume 1/4.
   for (int i = 0; i < max_tasklets/4; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });
      ASSERT_TRUE(res);
   }

   // Fill half of the buffer.
   for (int i = 0; i < max_tasklets/2; i++) {
      res = enqueue_tasklet(0, &simple_func1, TO_PTR(1234));
      ASSERT_TRUE(res);
   }

   // Consume 2/4
   for (int i = 0; i < max_tasklets/2; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });
      ASSERT_TRUE(res);
   }

   // Fill half of the buffer.
   for (int i = 0; i < max_tasklets/2; i++) {
      res = enqueue_tasklet(0, &simple_func1, TO_PTR(1234));
      ASSERT_TRUE(res);
   }

   // Now the cyclic buffer for sure rotated.

   // Consume 3/4
   for (int i = 0; i < 3*max_tasklets/4; i++) {
      ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });
      ASSERT_TRUE(res);
   }

   ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });

   // There are no more tasklets, expecting the RUN failed.
   ASSERT_FALSE(res);
}

TEST_F(tasklet_test, chaos)
{
   const int max_tasklets = get_tasklet_runner_limit(0);

   random_device rdev;
   default_random_engine e(rdev());

   lognormal_distribution<> dist(3.0, 2.5);

   int slots_used = 0;
   bool res = false;

   for (int iters = 0; iters < 10000; iters++) {

      int c;
      c = round(dist(e));

      for (int i = 0; i < c; i++) {

         if (slots_used == max_tasklets) {
            ASSERT_FALSE(enqueue_tasklet(0, &simple_func1, TO_PTR(1234)));
            break;
         }

         res = enqueue_tasklet(0, &simple_func1, TO_PTR(1234));
         ASSERT_TRUE(res);
         slots_used++;
      }

      c = round(dist(e));

      for (int i = 0; i < c; i++) {

         if (slots_used == 0) {
            ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });
            ASSERT_FALSE(res);
            break;
         }

         ASSERT_NO_FATAL_FAILURE({ res = run_one_tasklet(0); });
         ASSERT_TRUE(res);
         slots_used--;
      }
   }
}
