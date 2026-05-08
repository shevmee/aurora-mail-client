#include "MimeContext.hpp"

#include <gmime/gmime.h>

namespace aurora::mail::common::mime
{

  MimeContext::MimeContext()
  {
    g_mime_init();
  }

  MimeContext::~MimeContext()
  {
    g_mime_shutdown();
  }

  MimeContext& getMimeContext()
  {
    static MimeContext context;
    return context;
  }

}  // namespace aurora::mail::common::mime