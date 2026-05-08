#ifndef MIME_CONTEXT_HPP
#define MIME_CONTEXT_HPP

namespace aurora::mail::common::mime
{

  class MimeContext
  {
   public:
    MimeContext();
    ~MimeContext();
    MimeContext(const MimeContext&) = delete;
    MimeContext& operator=(const MimeContext&) = delete;
  };

  MimeContext& getMimeContext();
}  // namespace aurora::mail::common::mime

#endif  // MIME_CONTEXT_HPP
