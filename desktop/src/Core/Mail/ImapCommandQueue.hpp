#ifndef IMAP_COMMAND_QUEUE_HPP
#define IMAP_COMMAND_QUEUE_HPP

#include <deque>
#include <mutex>

#include "ImapSessionTypes.hpp"

/**
 * Serial IMAP command queue with coalescing rules (same semantics as previous MainWindow queue).
 */
class ImapCommandQueue
{
 public:
  void enqueue(ImapOperation op);

  [[nodiscard]] bool tryPop(ImapOperation& out);

  [[nodiscard]] bool empty() const;

 private:
  mutable std::mutex mutex_;
  std::deque<ImapOperation> queue_;
};

#endif
