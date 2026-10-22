// (Drawable / Logger) onto asf::dyn.

#include "asf_check.hpp"
#include "test_bind.hpp"
#include "cast.hpp"

#include <cmath>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace dyn_integration_tests_detail {

template <typename T = void>
struct Drawable;

template <>
struct ASF_TEST_TRAIT_MIRROR
Drawable<void> {
  void draw(auto const& self, std::ostream& out);
  double area(auto const& self);
};

class Rectangle {
 public:
  explicit Rectangle(double width, double height)
      : width_(width), height_(height) {}
  double width_{};
  double height_{};
  std::string payload_{asf_test::no_sso("rect")};
};

class Circle {
 public:
  explicit Circle(double radius) : radius_(radius) {}
  double radius_{};
  std::string payload_{asf_test::no_sso("circle")};
};

class Point {
 public:
  std::string payload_{asf_test::no_sso("point")};
};

template <>
struct Drawable<Rectangle> final {
  static void draw(ASF_TEST_CM(Rectangle) self, std::ostream& out) {
    out << "{Rectangle: width = " << self.width_
        << ", height = " << self.height_ << "}";
  }
  static double area(ASF_TEST_CM(Rectangle) self) {
    return self.width_ * self.height_;
  }
};

template <>
struct Drawable<Circle> final {
  static void draw(ASF_TEST_CM(Circle) self, std::ostream& out) {
    out << "{Circle: radius = " << self.radius_ << "}";
  }
  static double area(ASF_TEST_CM(Circle) self) {
    return std::numbers::pi * self.radius_ * self.radius_;
  }
};

template <>
struct Drawable<Point> final {
  static void draw(asf::c_mirror<Point>, std::ostream& out) { out << "{Point}"; }
  static double area(asf::c_mirror<Point>) { return 0; }
};

std::string PrintDrawableToString(asf::dyn<Drawable> p) {
  std::stringstream result;
  result << std::fixed << std::setprecision(5) << "shape = ";
  p->draw(result);
  result << ", area = " << p->area();
  return std::move(result).str();
}

std::vector<std::string> ParseCommand(const std::string& s) {
  std::vector<std::string> result(1u);
  bool in_quote = false;
  std::size_t last_valid = s.find_last_not_of(' ');
  for (std::size_t i = 0u; i <= last_valid; ++i) {
    if (s[i] == '`' && i < last_valid) {
      result.back() += s[++i];
    } else if (s[i] == '"') {
      in_quote = !in_quote;
    } else if (s[i] == ' ' && !in_quote) {
      if (!result.back().empty())
        result.emplace_back();
    } else {
      result.back() += s[i];
    }
  }
  if (result.back().empty())
    result.pop_back();
  return result;
}

// Owning Drawable (make/allocate analogue): flat Trait, no MI.
template <typename T = void>
struct OwnDrawable;

template <>
struct ASF_TEST_TRAIT_MIRROR
OwnDrawable<void> {
  void draw(auto const& self, std::ostream& out);
  double area(auto const& self);
  [[=asf_ann::imple]] auto op_construct(auto const& self) -> std::remove_cvref_t<decltype(self)>  { return self; }
};

template <>
struct OwnDrawable<Rectangle> final {
  static void draw(ASF_TEST_CM(Rectangle) self, std::ostream& out) {
    Drawable<Rectangle>::draw(self, out);
  }
  static double area(ASF_TEST_CM(Rectangle) self) {
    return Drawable<Rectangle>::area(self);
  }
};

asf::unique_dyn<OwnDrawable> MakeOwnedDrawable(const std::string& s) {
  std::vector<std::string> parsed = ParseCommand(s);
  if (!parsed.empty() && parsed[0u] == "Rectangle" && parsed.size() == 3u) {
    Rectangle tmp{std::stod(parsed[1u]), std::stod(parsed[2u])};
    auto mtmp = asf::to_mirror(tmp);
    asf::dyn<OwnDrawable> obs{mtmp};
    return asf::construct<std::unique_ptr>(*obs);
  }
  throw std::runtime_error{"Invalid command"};
}

asf::dyn<Drawable> MakeDrawableFromCommand(const std::string& s) {
  std::vector<std::string> parsed = ParseCommand(s);
  if (!parsed.empty()) {
    if (parsed[0u] == "Rectangle" && parsed.size() == 3u) {
      static Rectangle rect{0, 0};
      rect = Rectangle{std::stod(parsed[1u]), std::stod(parsed[2u])};
      return asf_test::to_dyn<Drawable>(rect);
    }
    if (parsed[0u] == "Circle" && parsed.size() == 2u) {
      static Circle circle{0};
      circle = Circle{std::stod(parsed[1u])};
      return asf_test::to_dyn<Drawable>(circle);
    }
    if (parsed[0u] == "Point" && parsed.size() == 1u) {
      static Point instance;
      return asf_test::to_dyn<Drawable>(instance);
    }
  }
  throw std::runtime_error{"Invalid command"};
}

// Same-name overloads on one Trait (FreeAsMem / member overload style).
template <typename T = void>
struct Logger;

template <>
struct ASF_TEST_TRAIT_MIRROR
Logger<void> {
  void log(auto& self, const char* s) {
    if constexpr (requires { *self.out_; })
      *self.out_ << "[INFO] " << s << "\n";
    else
      (void)self, (void)s;
  }
  void log(auto& self, const char* s, const std::exception& e) {
    if constexpr (requires { *self.out_; })
      *self.out_ << "[ERROR] " << s << " (exception info: " << e.what()
                 << ")\n";
    else
      (void)self, (void)s, (void)e;
  }
};

class StreamLogger {
 public:
  explicit StreamLogger(std::ostream& out) : out_(&out) {}
  std::ostream* out_{};
};

template <>
struct Logger<StreamLogger> final {};

void TestDrawable() {
  asf::dyn<Drawable> p = MakeDrawableFromCommand("Rectangle 2 3");
  std::string s = PrintDrawableToString(std::move(p));
  ASF_CHECK_EQ(s,
               "shape = {Rectangle: width = 2.00000, height = 3.00000}, "
               "area = 6.00000");

  p = MakeDrawableFromCommand("Circle 1");
  s = PrintDrawableToString(std::move(p));
  ASF_CHECK_EQ(s, "shape = {Circle: radius = 1.00000}, area = 3.14159");

  p = MakeDrawableFromCommand("Point");
  s = PrintDrawableToString(std::move(p));
  ASF_CHECK_EQ(s, "shape = {Point}, area = 0.00000");

  bool thrown = false;
  try {
    p = MakeDrawableFromCommand("Triangle 2 3");
  } catch (const std::runtime_error& e) {
    thrown = true;
    ASF_CHECK_STREQ(e.what(), "Invalid command");
  }
  ASF_CHECK(thrown);

  auto owned = MakeOwnedDrawable("Rectangle 2 3");
  ASF_CHECK(std::abs(owned->area() - 6.0) < 1e-9);
}

void TestLogger() {
  std::ostringstream out;
  StreamLogger logger{out};
  auto p = asf_test::to_dyn<Logger>(logger);
  p->log("hello");
  try {
    throw std::runtime_error{"runtime error!"};
  } catch (const std::exception& e) {
    p->log("world", e);
  }
  ASF_CHECK_EQ(std::move(out).str(),
               "[INFO] hello\n"
               "[ERROR] world (exception info: runtime error!)\n");
}

}  // namespace dyn_integration_tests_detail

int main() {
  std::fprintf(stdout, "dyn_integration_tests\n");
  ASF_RUN(dyn_integration_tests_detail::TestDrawable);
  ASF_RUN(dyn_integration_tests_detail::TestLogger);
  return asf_test::g_failures ? 1 : 0;
}
