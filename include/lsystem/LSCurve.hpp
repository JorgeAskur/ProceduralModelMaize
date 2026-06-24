#pragma once

// ---------------------------------------------------------------------------
// LSCurve.hpp — standalone replacement for EvoEngine's Utilities/Plot2D.hpp.
//
// The L-system tassel port depends on four EvoEngine types: Curve2D, Plot2D,
// SingleDistribution and PlottedDistribution. Upstream those types carry ImGui
// editor UIs and YAML (de)serialization. This tool needs neither — only the
// deterministic evaluation path that the production/growth rules call.
//
// The evaluation math (Curve2D::GetValue Bezier-with-tangents, the
// constructors, Clear, SetStart/SetEnd, and Plot2D::GetValue) is ported
// byte-for-byte from EvoEngine_SDK/src/Plot2D.cpp so the ported tassel rules
// reproduce upstream output exactly. The namespace is kept as `evo_engine`
// so the copied rule/helper headers compile unchanged.
// ---------------------------------------------------------------------------

#include <glm/glm.hpp>
#include <algorithm>
#include <vector>

namespace evo_engine {

// ---------------------------------------------------------------------------
// Curve2D — control-point curve, optionally with Bezier tangent handles.
// Storage layout for tangent mode: triplets of [in-tangent, point, out-tangent]
// where tangents are stored as offsets relative to their point.
// ---------------------------------------------------------------------------
class Curve2D {
  bool tangent_ = true;
  std::vector<glm::vec2> values_;
  glm::vec2 min_{0, 0};
  glm::vec2 max_{1, 1};

 public:
  explicit Curve2D(const glm::vec2& min = {0, 0}, const glm::vec2& max = {1, 1}, bool tangent = true) {
    tangent_ = tangent;
    min_ = min;
    max_ = max;
    Clear();
  }

  Curve2D(float start, float end, const glm::vec2& min = {0, 0}, const glm::vec2& max = {1, 1}, bool tangent = true) {
    min_ = min;
    max_ = max;
    start = glm::clamp(start, min_.y, max_.y);
    end = glm::clamp(end, min_.y, max_.y);
    tangent_ = tangent;
    if (!tangent_) {
      values_.clear();
      values_.emplace_back(min_.x, start);
      values_.emplace_back(max_.x, end);
    } else {
      values_.clear();
      values_.emplace_back(-(max_.y - min_.y) / 10.0f, 0.0f);
      values_.emplace_back(min_.x, start);
      values_.emplace_back((max_.y - min_.y) / 10.0f, 0.0f);

      values_.emplace_back(-(max_.y - min_.y) / 10.0f, 0.0f);
      values_.emplace_back(max_.x, end);
      values_.emplace_back((max_.y - min_.y) / 10.0f, 0.0f);
    }
  }

  void Clear() {
    if (!tangent_) {
      auto start = 0.0f;
      if (!values_.empty())
        start = values_.front().y;
      auto end = 0.0f;
      if (!values_.empty())
        end = values_.back().y;

      values_.clear();
      values_.emplace_back(min_.x, (min_.y + max_.y) / 2.0f);
      values_.emplace_back(max_.x, (min_.y + max_.y) / 2.0f);
      if (!values_.empty())
        SetStart(start);
      if (!values_.empty())
        SetEnd(end);
    } else {
      auto start = 0.0f;
      auto end = 0.0f;
      if (values_.size() >= 6)
        start = values_[1].y;
      if (values_.size() >= 6)
        end = values_[values_.size() - 2].y;

      values_.clear();
      values_.emplace_back(-(max_.y - min_.y) / 10.0f, 0.0f);
      values_.emplace_back(min_.x, (min_.y + max_.y) / 2.0f);
      values_.emplace_back((max_.y - min_.y) / 10.0f, 0.0f);

      values_.emplace_back(-(max_.y - min_.y) / 10.0f, 0.0f);
      values_.emplace_back(max_.x, (min_.y + max_.y) / 2.0f);
      values_.emplace_back((max_.y - min_.y) / 10.0f, 0.0f);

      if (values_.size() >= 6)
        SetStart(start);
      if (values_.size() >= 6)
        SetEnd(end);
    }
  }

  [[nodiscard]] std::vector<glm::vec2>& UnsafeGetValues() { return values_; }
  [[nodiscard]] const std::vector<glm::vec2>& UnsafeGetValues() const { return values_; }

  void SetTangent(bool value) { tangent_ = value; }
  [[nodiscard]] bool IsTangent() const { return tangent_; }

  void SetStart(float value) {
    if (!tangent_) {
      values_.front().y = glm::clamp(value, min_.y, max_.y);
    } else {
      values_[1].y = glm::clamp(value, min_.y, max_.y);
    }
  }

  void SetEnd(float value) {
    if (!tangent_) {
      values_.back().y = glm::clamp(value, min_.y, max_.y);
    } else {
      values_[values_.size() - 2].y = glm::clamp(value, min_.y, max_.y);
    }
  }

  [[nodiscard]] float GetValue(float x, const unsigned iteration = 8) const {
    x = glm::clamp(x, 0.0f, 1.0f);
    if (tangent_) {
      const int point_size = static_cast<int>(values_.size()) / 3;
      for (int i = 0; i < point_size - 1; i++) {
        auto& prev = values_[i * 3 + 1];
        auto& next = values_[i * 3 + 4];
        if (x == prev.x) {
          return prev.y;
        }
        if (x > prev.x && x < next.x) {
          const float real_x = (x - prev.x) / (next.x - prev.x);
          float upper = 1.0f;
          float lower = 0.0f;
          float temp_t = 0.5f;
          for (unsigned iteration_index = 0; iteration_index < iteration; iteration_index++) {
            const float temp_t1 = 1.0f - temp_t;
            const float global_x = temp_t1 * temp_t1 * temp_t1 * prev.x +
                                   3.0f * temp_t1 * temp_t1 * temp_t * (prev.x + values_[i * 3 + 2].x) +
                                   3.0f * temp_t1 * temp_t * temp_t * (next.x + values_[i * 3 + 3].x) +
                                   temp_t * temp_t * temp_t * next.x;
            if (const float test_x = (global_x - prev.x) / (next.x - prev.x); test_x > real_x) {
              upper = temp_t;
              temp_t = (temp_t + lower) / 2.0f;
            } else {
              lower = temp_t;
              temp_t = (temp_t + upper) / 2.0f;
            }
          }
          const float temp_t1 = 1.0f - temp_t;
          return temp_t1 * temp_t1 * temp_t1 * prev.y +
                 3.0f * temp_t1 * temp_t1 * temp_t * (prev.y + values_[i * 3 + 2].y) +
                 3.0f * temp_t1 * temp_t * temp_t * (next.y + values_[i * 3 + 3].y) +
                 temp_t * temp_t * temp_t * next.y;
        }
      }
      return values_[values_.size() - 2].y;
    }
    for (int i = 0; i < static_cast<int>(values_.size()) - 1; i++) {
      auto& prev = values_[i];
      if (auto& next = values_[i + 1]; x >= prev.x && x < next.x) {
        return prev.y + (next.y - prev.y) * (x - prev.x) / (next.x - prev.x);
      }
    }
    return values_[values_.size() - 1].y;
  }
};

// ---------------------------------------------------------------------------
// Plot2D<T> — a Curve2D remapped into [min_value, max_value].
// ---------------------------------------------------------------------------
template <class T>
struct Plot2D {
  T min_value = T(0);
  T max_value = T(1);
  Curve2D curve = Curve2D(0.5f, 0.5f, {0, 0}, {1, 1});

  Plot2D() = default;
  Plot2D(T min, T max, const Curve2D& c = Curve2D(0.5f, 0.5f, {0, 0}, {1, 1}))
      : min_value(min), max_value(max), curve(c) {}

  [[nodiscard]] T GetValue(float t) const {
    return glm::mix(min_value, max_value, glm::clamp(curve.GetValue(t), 0.0f, 1.0f));
  }
};

// ---------------------------------------------------------------------------
// SingleDistribution<T> — scalar mean + Gaussian deviation.
// Sampling is performed by the L-system rule helpers (SampleDistribution),
// which read .mean / .deviation directly; GetValue() here is the deterministic
// mean accessor used where no RNG is threaded.
// ---------------------------------------------------------------------------
template <class T>
struct SingleDistribution {
  T mean = T(0);
  float deviation = 0.0f;

  SingleDistribution() = default;
  SingleDistribution(T mean_value) : mean(mean_value) {}  // NOLINT: brace-init `{value}` convenience

  [[nodiscard]] T GetValue() const { return mean; }
};

// ---------------------------------------------------------------------------
// PlottedDistribution<T> — position-dependent mean + deviation curves.
// Rule helpers read .mean / .deviation (both Plot2D) directly via SamplePlotted.
// ---------------------------------------------------------------------------
template <class T>
struct PlottedDistribution {
  Plot2D<T> mean;
  Plot2D<float> deviation;

  [[nodiscard]] T GetValue(float t) const { return mean.GetValue(t); }
};

}  // namespace evo_engine
