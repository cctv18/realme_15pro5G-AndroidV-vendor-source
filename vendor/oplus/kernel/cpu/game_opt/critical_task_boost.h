#ifndef __CRITICAL_TASK_BOOST__
#define __CRITICAL_TASK_BOOST__

void ctb_notify_frame_produce(void);
void hrtimer_boost_init(void);
void hrtimer_boost_exit(void);
void reset_critical_task_time(void);

#endif // __CRITICAL_TASK_BOOST__
