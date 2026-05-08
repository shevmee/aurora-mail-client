#include "ImapCommandQueue.hpp"

#include <algorithm>

void ImapCommandQueue::enqueue(ImapOperation op)
{
    std::lock_guard<std::mutex> lock(mutex_);
    switch (op.type) {
        case ImapOpType::SelectMailbox:
            queue_.clear();
            queue_.push_back(std::move(op));
            break;

        case ImapOpType::LoadEmail:
            std::erase_if(queue_, [](const ImapOperation& o) {
                return o.type == ImapOpType::LoadEmail;
            });
            queue_.push_back(std::move(op));
            break;

        case ImapOpType::FetchMailboxPage:
            std::erase_if(queue_, [](const ImapOperation& o) {
                return o.type == ImapOpType::FetchMailboxPage;
            });
            queue_.push_back(std::move(op));
            break;

        default:
            queue_.push_back(std::move(op));
            break;
    }
}

bool ImapCommandQueue::tryPop(ImapOperation& out)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return false;
    }
    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

bool ImapCommandQueue::empty() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}
