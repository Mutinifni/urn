#pragma once

/**
 * \file urn/error.hpp
 * urn library error codes
 */

#include <urn/__bits/lib.hpp>
#include <system_error>


__urn_begin


#define __urn_errc(X_) \
  X_(__0, "internal placeholder for not an error") \
  X_(temporary_error, "temporary error")


/**
 * urn error codes
 */
enum class errc
{
  #define __urn_errc_list(code, message) code,
    __urn_errc(__urn_errc_list)
  #undef __urn_errc_list
};


/**
 * Return urn error category. The name() virtual function returns "urn".
 */
const std::error_category &error_category () noexcept;


/**
 * Make std::error_code from error code \a ec
 */
inline std::error_code make_error_code (errc ec) noexcept
{
  return std::error_code(static_cast<int>(ec), error_category());
}


__urn_end


namespace std {

template <>
struct is_error_code_enum<urn::errc>
    : true_type
{ };

} // namespace std
