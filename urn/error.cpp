#include <urn/error.hpp>
#include <string>


__urn_begin


namespace {

constexpr std::string_view to_string_view (errc ec) noexcept
{
  switch (ec)
  {
    #define __urn_errc_case(code, message) case urn::errc::code: return message;
      __urn_errc(__urn_errc_case)
    #undef __urn_errc_case

    default:
      return "unknown";
  };
}

} // namespace


const std::error_category &error_category () noexcept
{
  struct error_category_impl
    : public std::error_category
  {
    [[nodiscard]] const char *name () const noexcept final
    {
      return "urn";
    }

    [[nodiscard]] std::string message (int ec) const final
    {
      return std::string{to_string_view(static_cast<errc>(ec))};
    }
  };
  static const error_category_impl impl{};
  return impl;
}


__urn_end
