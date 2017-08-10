#include "indexedScheduler.hpp"
#include "posManager.hpp"

namespace workstealing { namespace indexed {

void stopScheduler() {
  running.store(false);
  tasks_required_sem.signal(1);
}

void startScheduler(std::vector<hpx::naming::id_type> posManagers) {
  auto schedulerReady = std::make_shared<hpx::promise<void> >();

  if (hpx::get_os_thread_count() > 1) {
    hpx::threads::executors::default_executor high_priority_executor(hpx::threads::thread_priority_critical,
                                                                     hpx::threads::thread_stacksize_large);
    hpx::apply(high_priority_executor, &scheduler, posManagers, schedulerReady);
  } else {
    hpx::threads::executors::default_executor exe(hpx::threads::thread_stacksize_large);
    hpx::apply(exe, &scheduler, posManagers, schedulerReady);
  }

  schedulerReady->get_future().get();
}

void scheduler(std::vector<hpx::naming::id_type> posManagers, std::shared_ptr<hpx::promise<void> >readyPromise) {
  auto here = hpx::find_here();
  hpx::naming::id_type posManager;

  // Find the local posManager
  for (auto it = posManagers.begin(); it != posManagers.end(); ++it) {
    if (hpx::get_colocation_id(*it).get() == here) {
      posManager = *it;
      posManagers.erase(it);
      break;
    }
  }

  // Register all other posManagers with the local manager
  hpx::async<workstealing::indexed::posManager::registerDistributedManagers_action>(posManager, posManagers);

  // posManager variables are set up, we can start generating tasks
  readyPromise->set_value();

  auto threads = hpx::get_os_thread_count();
  hpx::threads::executors::current_executor scheduler;

  // Pre-init the sem
  if (threads > 1) {
    // Don't both scheduling in the one thread case since we never need more
    // work in the indexed scheme
    tasks_required_sem.signal(threads - 1);
  }

  while (running) {
    tasks_required_sem.wait();

    auto scheduled = hpx::async<workstealing::indexed::posManager::getWork_action>(posManager).get();
    if (!scheduled) {
      hpx::this_thread::suspend(10);
      tasks_required_sem.signal();
    }
  }
}
}}