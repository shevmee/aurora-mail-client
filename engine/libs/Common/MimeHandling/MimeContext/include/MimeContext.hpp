#ifndef MIME_CONTEXT_HPP
#define MIME_CONTEXT_HPP

namespace aurora::mail::common::mime
{

  /**
   * @brief Process-wide RAII guard that initialises and shuts down the GMime
   *        runtime exactly once.
   *
   * GMime requires a one-time call to `g_mime_init()` before any of its
   * parsing/serialisation routines are used and a matching `g_mime_shutdown()`
   * at process exit. `MimeContext` wraps that contract so the rest of the
   * codebase never has to think about it.
   *
   * The instance is intentionally non-copyable and non-movable; obtain the
   * shared instance via @ref getMimeContext().
   */
  class MimeContext
  {
   public:
    MimeContext();
    ~MimeContext();
    MimeContext(const MimeContext&) = delete;
    MimeContext& operator=(const MimeContext&) = delete;
  };

  /**
   * @brief Returns the process-wide GMime context, constructing it on first
   *        use (Meyers singleton).
   *
   * Callers should invoke this once during application startup before any
   * MIME parsing/serialisation. Subsequent calls are O(1) and thread-safe
   * after C++11.
   */
  MimeContext& getMimeContext();
}  // namespace aurora::mail::common::mime

#endif  // MIME_CONTEXT_HPP
