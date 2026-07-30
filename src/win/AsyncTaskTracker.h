#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <list>
#include <thread>
#include <utility>

namespace sf::win {

using OperationId = std::uint64_t;

class AsyncTaskTracker final {
public:
  AsyncTaskTracker() = default;
  ~AsyncTaskTracker() = default;

  AsyncTaskTracker(const AsyncTaskTracker &) = delete;
  AsyncTaskTracker &operator=(const AsyncTaskTracker &) = delete;

  [[nodiscard]] OperationId NextId() noexcept {
    const OperationId id = nextId_++;
    if (nextId_ == 0)
      nextId_ = 1;
    return id;
  }

  void Track(OperationId id, std::jthread task) {
    tasks_.push_back({id, std::move(task)});
  }

  [[nodiscard]] bool Complete(OperationId id) {
    const auto found =
        std::find_if(tasks_.begin(), tasks_.end(),
                     [id](const Task &task) { return task.id == id; });
    if (found == tasks_.end())
      return false;
    tasks_.erase(found);
    return true;
  }

  [[nodiscard]] std::size_t Size() const noexcept { return tasks_.size(); }

private:
  struct Task final {
    OperationId id = 0;
    std::jthread thread;
  };

  OperationId nextId_ = 1;
  std::list<Task> tasks_;
};

} // namespace sf::win
