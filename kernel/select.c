/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/process.h>

static void
debug_dump_fds(const char *name, int nfds, fd_set *s)
{
   if (s) {

      printk("    %s: [ ", name);

      for (int i = 0; i < nfds; i++)
         if (FD_ISSET(i, s))
            printk(NO_PREFIX "%d ", i);

      printk(NO_PREFIX "]\n");

   } else {
      printk("    %s: NULL,\n", name);
   }
}

static void
debug_dump_select_args(int nfds, fd_set *rfds, fd_set *wfds,
                       fd_set *efds, struct timeval *tv)
{
   printk("sys_select(\n");
   printk("    nfds: %d,\n", nfds);

   debug_dump_fds("rfds", nfds, rfds);
   debug_dump_fds("wfds", nfds, wfds);
   debug_dump_fds("efds", nfds, efds);

   if (tv)
      printk("    tv: %u secs, %u usecs\n", tv->tv_sec, tv->tv_usec);
   else
      printk("    tv: NULL\n");

   printk(")\n");
}

static int
select_count_kcond(u32 nfds,
                   fd_set *set,
                   u32 *cond_cnt_ref,
                   func_get_rwe_cond get_cond)
{
   if (!set)
      return 0;

   for (u32 i = 0; i < nfds; i++) {

      if (!FD_ISSET(i, set))
         continue;

      fs_handle h = get_fs_handle(i);

      if (!h)
         return -EBADF;

      if (get_cond(h))
         (*cond_cnt_ref)++;
   }

   return 0;
}

static int
select_set_kcond(u32 nfds,
                 multi_obj_waiter *w,
                 u32 *idx,
                 fd_set *set,
                 func_get_rwe_cond get_cond)
{
   fs_handle h;
   kcond *c;

   if (!set)
      return 0;

   for (u32 i = 0; i < nfds; i++) {

      if (!FD_ISSET(i, set))
         continue;

      if (!(h = get_fs_handle(i)))
         return -EBADF;

      c = get_cond(h);
      ASSERT((*idx) < w->count);

      if (c)
         mobj_waiter_set(w, (*idx)++, WOBJ_KCOND, c, &c->wait_list);
   }

   return 0;
}

static int
select_set_ready(u32 nfds, fd_set *set, func_rwe_ready is_ready)
{
   int tot = 0;

   if (!set)
      return tot;

   for (u32 i = 0; i < nfds; i++) {

      if (!FD_ISSET(i, set))
         continue;

      fs_handle h = get_fs_handle(i);

      if (!h || !is_ready(h)) {
         FD_CLR(i, set);
      } else {
         tot++;
      }
   }

   return tot;
}

static const func_get_rwe_cond gcf[3] = {
   &vfs_get_rready_cond,
   &vfs_get_wready_cond,
   &vfs_get_except_cond
};

static const func_rwe_ready grf[3] = {
   &vfs_read_ready,
   &vfs_write_ready,
   &vfs_except_ready
};

static u32
count_signaled_conds(multi_obj_waiter *w)
{
   u32 count = 0;

   for (u32 j = 0; j < w->count; j++) {

      mwobj_elem *me = &w->elems[j];

      if (me->type && !me->wobj.type) {
         count++;
         mobj_waiter_reset(me);
      }
   }

   return count;
}

static u32
count_ready_streams_per_set(u32 nfds, fd_set *set, func_rwe_ready is_ready)
{
   u32 count = 0;

   if (!set)
      return count;

   for (u32 j = 0; j < nfds; j++) {

      if (!FD_ISSET(j, set))
         continue;

      fs_handle h = get_fs_handle(j);

      if (h && is_ready(h))
         count++;
   }

   return count;
}

static u32
count_ready_streams(u32 nfds, fd_set *sets[3])
{
   u32 count = 0;

   for (int i = 0; i < 3; i++) {
      count += count_ready_streams_per_set(nfds, sets[i], grf[i]);
   }

   return count;
}

static int
select_wait_on_cond(u32 nfds,
                    fd_set *sets[3],
                    struct timeval *tv,
                    u32 cond_cnt,
                    u32 timeout_ticks)
{
   task_info *curr = get_curr_task();
   multi_obj_waiter *waiter = NULL;
   u32 idx = 0;
   int rc = 0;

   /*
    * NOTE: it is not that difficult cond_cnt to be 0: it's enough the
    * specified files to NOT have r/w/e get kcond functions. Also, all the
    * sets might be NULL (see the comment below).
    */

   if (!(waiter = allocate_mobj_waiter(cond_cnt)))
      return -ENOMEM;

   for (int i = 0; i < 3; i++) {
      if ((rc = select_set_kcond((u32)nfds, waiter, &idx, sets[i], gcf[i])))
         goto out;
   }

   if (tv) {
      ASSERT(timeout_ticks > 0);
      task_set_wakeup_timer(get_curr_task(), timeout_ticks);
   }

   while (true) {

      kernel_sleep_on_waiter(waiter);

      if (tv) {

         if (curr->wobj.type) {

            /* we woke-up because of the timeout */
            wait_obj_reset(&curr->wobj);
            tv->tv_sec = 0;
            tv->tv_usec = 0;

         } else {

            /*
             * We woke-up because of a kcond was signaled, but that does NOT
             * mean that even the signaled conditions correspond to ready
             * streams. We have to check that.
             */

            if (!count_ready_streams(nfds, sets))
               continue; /* No ready streams, we have to wait again. */

            u32 rem = task_cancel_wakeup_timer(curr);
            tv->tv_sec = rem / TIMER_HZ;
            tv->tv_usec = (rem % TIMER_HZ) * (1000000 / TIMER_HZ);
         }

      } else {

         /* No timeout: we woke-up because of a kcond was signaled */

         if (!count_ready_streams(nfds, sets))
            continue; /* No ready streams, we have to wait again. */
      }

      break;
   }

out:
   free_mobj_waiter(waiter);
   return rc;
}

sptr sys_select(int user_nfds, fd_set *user_rfds, fd_set *user_wfds,
                fd_set *user_efds, struct timeval *user_tv)
{
   fd_set *u_sets[3] = { user_rfds, user_wfds, user_efds };

   task_info *curr = get_curr_task();
   int total_ready_count = 0;
   struct timeval *tv = NULL;
   u32 nfds = (u32)user_nfds;
   fd_set *sets[3] = {0};
   u32 cond_cnt = 0;
   u32 timeout_ticks = 0;
   int rc;

   if (user_nfds < 0 || user_nfds > MAX_HANDLES)
      return -EINVAL;

   for (int i = 0; i < 3; i++) {

      if (!u_sets[i])
         continue;

      sets[i] = ((fd_set*) curr->args_copybuf) + i;

      if (copy_from_user(sets[i], u_sets[i], sizeof(fd_set)))
         return -EFAULT;
   }

   if (user_tv) {

      tv = (void *) ((fd_set *) curr->args_copybuf) + 3;

      if (copy_from_user(tv, user_tv, sizeof(struct timeval)))
         return -EFAULT;

      u64 tmp = 0;
      tmp += (u64)tv->tv_sec * TIMER_HZ;
      tmp += (u64)tv->tv_usec / (1000000ull / TIMER_HZ);

      /* NOTE: select() can't sleep for more than UINT32_MAX ticks */
      timeout_ticks = (u32) MIN(tmp, UINT32_MAX);
   }

   //debug_dump_select_args(nfds, sets[0], sets[1], sets[2], tv);

   if (!tv || timeout_ticks > 0) {
      for (int i = 0; i < 3; i++) {
         if ((rc = select_count_kcond(nfds, sets[i], &cond_cnt, gcf[i])))
            return rc;
      }
   }

   if (cond_cnt > 0) {

      if ((rc = select_wait_on_cond(nfds, sets, tv, cond_cnt, timeout_ticks)))
         return rc;

   } else {

      if (timeout_ticks > 0) {

         /*
          * Corner case: no conditions on which to wait, but timeout is > 0:
          * this is still a valid case. Many years ago the following call:
          *    select(0, NULL, NULL, NULL, &tv)
          * was even used as a portable implementation of nanosleep().
          */

         kernel_sleep(timeout_ticks);
      }
   }

   for (int i = 0; i < 3; i++) {

      total_ready_count += select_set_ready(nfds, sets[i], grf[i]);

      if (u_sets[i] && copy_to_user(u_sets[i], sets[i], sizeof(fd_set)))
         return -EFAULT;
   }

   if (tv && copy_to_user(user_tv, tv, sizeof(struct timeval)))
      return -EFAULT;

   return total_ready_count;
}