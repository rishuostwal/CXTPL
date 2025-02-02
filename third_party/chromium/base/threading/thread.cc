﻿// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/thread.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/lazy_instance.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/third_party/dynamic_annotations/dynamic_annotations.h"
#include "base/threading/thread_id_name_manager.h"
#include "base/threading/thread_local.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"

#if defined(OS_POSIX) && !defined(OS_NACL) && !defined(OS_EMSCRIPTEN)
#include "base/files/file_descriptor_watcher_posix.h"
#endif

#if defined(OS_WIN)
#include "base/win/scoped_com_initializer.h"
#endif

#if (defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS))
/// \note only single thread = only one message loop
static std::unique_ptr<base::MessageLoop> g_message_loop;
#endif

#if (defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS))
/// \note only one main thread = only one run loop
static base::RunLoop* run_loop;
#endif

#if (defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS))
static bool hasPlatformThread = false;
#endif

namespace base {

namespace {

// We use this thread-local variable to record whether or not a thread exited
// because its Stop method was called.  This allows us to catch cases where
// MessageLoop::QuitWhenIdle() is called directly, which is unexpected when
// using a Thread to setup and run a MessageLoop.
base::LazyInstance<base::ThreadLocalBoolean>::Leaky lazy_tls_bool =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

Thread::Options::Options() = default;

Thread::Options::Options(MessageLoop::Type type, size_t size)
    : message_loop_type(type), stack_size(size) {}

Thread::Options::Options(Options&& other) = default;

Thread::Options::~Options() = default;

Thread::Thread(const std::string& name)
    : id_event_(WaitableEvent::ResetPolicy::MANUAL,
                WaitableEvent::InitialState::NOT_SIGNALED),
      name_(name),
      start_event_(WaitableEvent::ResetPolicy::MANUAL,
                   WaitableEvent::InitialState::NOT_SIGNALED) {
  // Only bind the sequence on Start(): the state is constant between
  // construction and Start() and it's thus valid for Start() to be called on
  // another sequence as long as every other operation is then performed on that
  // sequence.
  owning_sequence_checker_.DetachFromSequence();
}

Thread::~Thread() {
  Stop();
}

bool Thread::Start() {
  DCHECK(owning_sequence_checker_.CalledOnValidSequence());

  Options options;
#if defined(OS_WIN)
  if (com_status_ == STA)
    options.message_loop_type = MessageLoop::TYPE_UI;
#elif defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
options.message_loop_type = MessageLoop::TYPE_DEFAULT;
/// \note you can set options.joinable = false;
#endif
  return StartWithOptions(options);
}

bool Thread::StartWithOptions(const Options& options) {
  DCHECK(owning_sequence_checker_.CalledOnValidSequence());
  DCHECK(!task_environment_);
  DCHECK(!IsRunning());
  DCHECK(!stopping_) << "Starting a non-joinable thread a second time? That's "
                     << "not allowed!";
#if defined(OS_WIN)
  DCHECK((com_status_ != STA) ||
      (options.message_loop_type == MessageLoop::TYPE_UI));
#endif

  // Reset |id_| here to support restarting the thread.
  id_event_.Reset();
  id_ = kInvalidThreadId;

  SetThreadWasQuitProperly(false);

  timer_slack_ = options.timer_slack;

#if !(defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS))
  if (options.task_environment) {
    DCHECK(!options.message_pump_factory);
    task_environment_ = WrapUnique(options.task_environment);
  } else if (options.message_pump_factory) {
    task_environment_ = std::make_unique<internal::MessageLoopTaskEnvironment>(
        MessageLoop::CreateUnbound(options.message_pump_factory.Run()));
  } else {
    task_environment_ = std::make_unique<internal::MessageLoopTaskEnvironment>(
        MessageLoop::CreateUnbound(options.message_loop_type));
  }
#else
  if(!g_message_loop) {
      g_message_loop = MessageLoop::CreateUnbound(MessageLoop::TYPE_DEFAULT);
  }
  if(!task_environment_) {
      task_environment_ = std::make_unique<internal::MessageLoopTaskEnvironment>(
          //std::move(g_message_loop));
          nullptr);
  }
#endif

/*#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
  return false;
#else*/
  start_event_.Reset();

#if !(defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS))
  // Hold |thread_lock_| while starting the new thread to synchronize with
  // Stop() while it's not guaranteed to be sequenced (until crbug/629139 is
  // fixed).
  {
    AutoLock lock(thread_lock_);
    bool success =
        options.joinable
            ? PlatformThread::CreateWithPriority(options.stack_size, this,
                                                 &thread_, options.priority)
            : PlatformThread::CreateNonJoinableWithPriority(
                  options.stack_size, this, options.priority);
    if (!success) {
      DLOG(ERROR) << "failed to create thread";
      return false;
    }
  }

  joinable_ = options.joinable;
#else
  if(!hasPlatformThread) {
    hasPlatformThread = true;
    AutoLock lock(thread_lock_);
    bool success =
        options.joinable
        ? PlatformThread::CreateWithPriority(options.stack_size, this,
                                             &thread_, options.priority)
        : PlatformThread::CreateNonJoinableWithPriority(
              options.stack_size, this, options.priority);
    if (!success) {
        DLOG(ERROR) << "failed to create thread";
        return false;
    }
  }

  joinable_ = options.joinable;
#endif


  return true;
//#endif
}

bool Thread::StartAndWaitForTesting() {
#if defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS)
    P_LOG("TODO: Thread::FlushForTesting\n");
    HTML5_STACKTRACE();
#endif
  DCHECK(owning_sequence_checker_.CalledOnValidSequence());
  bool result = Start();
  if (!result)
    return false;
  WaitUntilThreadStarted();
  return true;
}

bool Thread::WaitUntilThreadStarted() const {
  DCHECK(owning_sequence_checker_.CalledOnValidSequence());
  if (!task_environment_)
    return false;
  // https://crbug.com/918039
  base::ScopedAllowBaseSyncPrimitivesOutsideBlockingScope allow_wait;
  start_event_.Wait();
  return true;
}

void Thread::FlushForTesting() {
#if defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS)
    P_LOG("TODO: Thread::FlushForTesting\n");
    HTML5_STACKTRACE();
#endif

  DCHECK(owning_sequence_checker_.CalledOnValidSequence());
  if (!task_environment_)
    return;

  WaitableEvent done(WaitableEvent::ResetPolicy::AUTOMATIC,
                     WaitableEvent::InitialState::NOT_SIGNALED);
#if defined(OS_EMSCRIPTEN)
  DCHECK(task_runner());
  if (!task_runner()){
      P_LOG("invalid task_runner\n");
  }
#endif
  task_runner()->PostTask(FROM_HERE,
                          BindOnce(&WaitableEvent::Signal, Unretained(&done)));
  done.Wait();
}

void Thread::Stop() {
#if defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS)
  // no check
#else
  DCHECK(joinable_);
#endif

  // TODO(gab): Fix improper usage of this API (http://crbug.com/629139) and
  // enable this check, until then synchronization with Start() via
  // |thread_lock_| is required...
  // DCHECK(owning_sequence_checker_.CalledOnValidSequence());
  AutoLock lock(thread_lock_);

  StopSoon();

  // Can't join if the |thread_| is either already gone or is non-joinable.
  if (thread_.is_null())
    return;

  // Wait for the thread to exit.
  //
  // TODO(darin): Unfortunately, we need to keep |task_environment_| around
  // until the thread exits. Some consumers are abusing the API. Make them stop.
  PlatformThread::Join(thread_);
  thread_ = base::PlatformThreadHandle();

  // The thread should release |task_environment_| on exit (note: Join() adds
  // an implicit memory barrier and no lock is thus required for this check).
  DCHECK(!task_environment_);

  stopping_ = false;
}

void Thread::StopSoon() {
  // TODO(gab): Fix improper usage of this API (http://crbug.com/629139) and
  // enable this check.
  // DCHECK(owning_sequence_checker_.CalledOnValidSequence());

  if (stopping_ || !task_environment_)
    return;

  stopping_ = true;

#if (defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS))
  if (!task_runner()){
      P_LOG("invalid task_runner\n");
      return;
  }
  DCHECK(task_runner());
#endif

//#if !(defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS))
  task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&Thread::ThreadQuitHelper, Unretained(this)));
//#endif
}

void Thread::DetachFromSequence() {
  DCHECK(owning_sequence_checker_.CalledOnValidSequence());
  owning_sequence_checker_.DetachFromSequence();
}

PlatformThreadId Thread::GetThreadId() const {
  // If the thread is created but not started yet, wait for |id_| being ready.
  base::ScopedAllowBaseSyncPrimitivesOutsideBlockingScope allow_wait;
  id_event_.Wait();
  return id_;
}

bool Thread::IsRunning() const {
  // TODO(gab): Fix improper usage of this API (http://crbug.com/629139) and
  // enable this check.
  // DCHECK(owning_sequence_checker_.CalledOnValidSequence());

  // If the thread's already started (i.e. |task_environment_| is non-null) and
  // not yet requested to stop (i.e. |stopping_| is false) we can just return
  // true. (Note that |stopping_| is touched only on the same sequence that
  // starts / started the new thread so we need no locking here.)
  if (task_environment_ && !stopping_)
    return true;
  // Otherwise check the |running_| flag, which is set to true by the new thread
  // only while it is inside Run().
  AutoLock lock(running_lock_);
  return running_;
}

void Thread::Run(RunLoop* run_loop) {
  // Overridable protected method to be called from our |thread_| only.
  DCHECK(id_event_.IsSignaled());
  DCHECK_EQ(id_, PlatformThread::CurrentId());

#if defined(OS_EMSCRIPTEN)
  DCHECK(run_loop);
#endif
  run_loop->Run();
}

// static
void Thread::SetThreadWasQuitProperly(bool flag) {
#if defined(OS_EMSCRIPTEN)
    DCHECK(lazy_tls_bool.Pointer());
#endif
  lazy_tls_bool.Pointer()->Set(flag);
}

// static
bool Thread::GetThreadWasQuitProperly() {
#if (defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS))
    return true;
#endif
  bool quit_properly = true;
#if DCHECK_IS_ON() && !defined(DISABLE_PTHREADS)
  quit_properly = lazy_tls_bool.Pointer()->Get();
#endif
  return quit_properly;
}

void Thread::ThreadMain() {
  // First, make GetThreadId() available to avoid deadlocks. It could be called
  // any place in the following thread initialization code.
  DCHECK(!id_event_.IsSignaled());
  // Note: this read of |id_| while |id_event_| isn't signaled is exceptionally
  // okay because ThreadMain has a happens-after relationship with the other
  // write in StartWithOptions().
  DCHECK_EQ(kInvalidThreadId, id_);
  id_ = PlatformThread::CurrentId();
  DCHECK_NE(kInvalidThreadId, id_);
  id_event_.Signal();

  // Complete the initialization of our Thread object.
  PlatformThread::SetName(name_.c_str());
  ANNOTATE_THREAD_NAME(name_.c_str());  // Tell the name to race detector.

  // Lazily initialize the |message_loop| so that it can run on this thread.
  DCHECK(task_environment_);
  // This binds MessageLoopCurrent and ThreadTaskRunnerHandle.
  task_environment_->BindToCurrentThread(timer_slack_);
#if !(defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS))
  DCHECK(MessageLoopCurrent::Get());
  DCHECK(ThreadTaskRunnerHandle::IsSet());
#endif

#if defined(OS_POSIX) && !defined(OS_NACL) && !defined(OS_EMSCRIPTEN)
  // Allow threads running a MessageLoopForIO to use FileDescriptorWatcher API.
  std::unique_ptr<FileDescriptorWatcher> file_descriptor_watcher;
  if (MessageLoopCurrentForIO::IsSet()) {
    file_descriptor_watcher.reset(
        new FileDescriptorWatcher(task_environment_->GetDefaultTaskRunner()));
  }
#endif

#if defined(OS_WIN)
  std::unique_ptr<win::ScopedCOMInitializer> com_initializer;
  if (com_status_ != NONE) {
    com_initializer.reset((com_status_ == STA) ?
        new win::ScopedCOMInitializer() :
        new win::ScopedCOMInitializer(win::ScopedCOMInitializer::kMTA));
  }
#endif

  // Let the thread do extra initialization.
  Init();

  {
    AutoLock lock(running_lock_);
    running_ = true;
  }

  start_event_.Signal();

#if (defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS))
  if(!run_loop) {
    run_loop = new RunLoop();
  }
  run_loop_ = run_loop;
#else
  RunLoop run_loop;
  run_loop_ = &run_loop;
#endif

  Run(run_loop_);


#if !(defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS))
  {
    AutoLock lock(running_lock_);
    running_ = false;
  }

  // Let the thread do extra cleanup.
  CleanUp();

#if defined(OS_WIN)
  com_initializer.reset();
#endif

  DCHECK(GetThreadWasQuitProperly());

  // We can't receive messages anymore.
  // (The message loop is destructed at the end of this block)
  task_environment_.reset();
  run_loop_ = nullptr;
#endif
}

void Thread::ThreadQuitHelper() {
#if !(defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS))
  DCHECK(run_loop_);
  run_loop_->QuitWhenIdle();
#endif
  SetThreadWasQuitProperly(true);
}

namespace internal {

MessageLoopTaskEnvironment::MessageLoopTaskEnvironment(
    std::unique_ptr<MessageLoop> message_loop)
#if !(defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS))
    : message_loop_(std::move(message_loop))
#endif
  {}

MessageLoopTaskEnvironment::~MessageLoopTaskEnvironment() {}

scoped_refptr<SingleThreadTaskRunner>
MessageLoopTaskEnvironment::GetDefaultTaskRunner() {
#if !(defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS))
    DCHECK(message_loop_);
    return message_loop_->task_runner();
#else
    DCHECK(g_message_loop);
    return g_message_loop->task_runner();
#endif
}

void MessageLoopTaskEnvironment::BindToCurrentThread(TimerSlack timer_slack) {
#if !(defined(OS_EMSCRIPTEN) && defined(DISABLE_PTHREADS))
    DCHECK(message_loop_);
    message_loop_->BindToCurrentThread();
    message_loop_->SetTimerSlack(timer_slack);
#else
    DCHECK(g_message_loop);
    g_message_loop->BindToCurrentThread();
    g_message_loop->SetTimerSlack(timer_slack);
#endif
}

}  // namespace internal

}  // namespace base
