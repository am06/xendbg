//
// Created by Spencer Michaels on 8/12/18.
//

#ifndef XENDBG_TOKEN_PREDICATE_HPP
#define XENDBG_TOKEN_PREDICATE_HPP

#include <variant>

#include "Token/Constant.hpp"
#include "Token/Label.hpp"
#include "Token/Symbol.hpp"
#include "Token/Variable.hpp"

namespace xd::parser::expr::op {
  class Sentinel;
}

namespace xd::parser::pred {

  template<typename Token_t>
  bool is_sentinel(const Token_t& token) {
    return std::holds_alternative<expr::op::Sentinel>(token);
  }

  template <typename Token_t>
  bool is_constant(const Token_t& token) {
    return std::holds_alternative<token::Constant>(token);
  }

  template <typename Token_t>
  bool is_label(const Token_t& token) {
    return std::holds_alternative<token::Label>(token);
  }

  template <typename Token_t>
  bool is_symbol(const Token_t& token) {
    return std::holds_alternative<token::Symbol>(token);
  }

  template <typename Token_t>
  bool is_symbol_of_type(const Token_t& token, const token::Symbol::Type& sym) {
    return std::holds_alternative<token::Symbol>(token) && std::get<token::Symbol>(token).type() == sym;
  }

  template <typename Token_t>
  bool is_variable(const Token_t& token) {
    return std::holds_alternative<token::Variable>(token);
  }

  template <typename Token_t>
  bool is_binary_operator_symbol(const Token_t& token) {
    return is_symbol_of_type(token, token::Symbol::Type::Plus) ||
           is_symbol_of_type(token, token::Symbol::Type::Minus) ||
           is_symbol_of_type(token, token::Symbol::Type::Star) ||
           is_symbol_of_type(token, token::Symbol::Type::Slash) ||
           is_symbol_of_type(token, token::Symbol::Type::Equals);
  };

  template <typename Token_t>
  bool is_unary_operator_symbol(const Token_t& token) {
    return is_symbol_of_type(token, token::Symbol::Type::Minus) ||
           is_symbol_of_type(token, token::Symbol::Type::Star);
  };

}

#endif //XENDBG_PREDICATE_HPP
