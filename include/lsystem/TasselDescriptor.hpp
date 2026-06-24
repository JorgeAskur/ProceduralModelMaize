#pragma once

// ---------------------------------------------------------------------------
// TasselDescriptor — standalone genotype for the ported maize-tassel L-system.
//
// Replaces EvoEngine's MaizeTasselDescriptor (an IAsset with ImGui/YAML/Entity
// machinery). This struct keeps only the parameter fields and the Sample()
// method that converts per-instance distribution draws into SampledTasselParams.
//
// The Sample() body is ported byte-for-byte from MaizeTasselDescriptor::Sample
// so the tassel topology/growth reproduces the upstream tuned behaviour. The
// tuned default values are applied by ApplyMaizeTasselDefaults() (generated
// from MaizeTasselDescriptor_Default.mtassel; see TasselDescriptorDefaults.cpp).
// ---------------------------------------------------------------------------

#include "LSCurve.hpp"
#include "MaizeTasselRules.hpp"        // SampledTasselParams, helpers
#include "LSystemRuleHelpers.hpp"      // SampleDistribution, SampleUnit01, TropismEntry
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace l_system_plugin {

struct TasselDescriptor {
  // ===== Branch Zone (lower rachis — lateral branches) =====
  evo_engine::SingleDistribution<float> branch_node_count{5.0f};
  evo_engine::PlottedDistribution<float> branch_internode_length;
  evo_engine::PlottedDistribution<float> branch_internode_thickness;
  evo_engine::PlottedDistribution<float> lateral_insertion_angle;
  evo_engine::PlottedDistribution<float> lateral_internode_length;
  evo_engine::PlottedDistribution<float> lateral_node_count;
  evo_engine::PlottedDistribution<float> peduncle_branch_probability;

  // ===== Central Spike (upper rachis) =====
  evo_engine::SingleDistribution<float> spike_node_count{8.0f};
  evo_engine::PlottedDistribution<float> spike_internode_length;
  evo_engine::PlottedDistribution<float> spike_internode_thickness;
  evo_engine::PlottedDistribution<float> spike_zone_branch_probability;

  // ===== Main-rachis spikelet pair morphology =====
  evo_engine::PlottedDistribution<float> main_pair_proximal_scale_x;
  evo_engine::PlottedDistribution<float> main_pair_proximal_scale_y;
  evo_engine::PlottedDistribution<float> main_pair_proximal_scale_z;
  evo_engine::PlottedDistribution<float> main_pair_proximal_angle;
  evo_engine::PlottedDistribution<float> main_pair_internode_length;
  evo_engine::PlottedDistribution<float> main_pair_internode_thickness;
  evo_engine::PlottedDistribution<float> main_pair_internode_angle;
  evo_engine::PlottedDistribution<float> main_pair_distal_scale_x;
  evo_engine::PlottedDistribution<float> main_pair_distal_scale_y;
  evo_engine::PlottedDistribution<float> main_pair_distal_scale_z;
  evo_engine::PlottedDistribution<float> main_pair_distal_angle;

  // ===== Non-main-axis spikelet pair morphology =====
  evo_engine::PlottedDistribution<float> branch_pair_proximal_scale_x;
  evo_engine::PlottedDistribution<float> branch_pair_proximal_scale_y;
  evo_engine::PlottedDistribution<float> branch_pair_proximal_scale_z;
  evo_engine::PlottedDistribution<float> branch_pair_proximal_angle;
  evo_engine::PlottedDistribution<float> branch_pair_internode_length;
  evo_engine::PlottedDistribution<float> branch_pair_internode_thickness;
  evo_engine::PlottedDistribution<float> branch_pair_internode_angle;
  evo_engine::PlottedDistribution<float> branch_pair_distal_scale_x;
  evo_engine::PlottedDistribution<float> branch_pair_distal_scale_y;
  evo_engine::PlottedDistribution<float> branch_pair_distal_scale_z;
  evo_engine::PlottedDistribution<float> branch_pair_distal_angle;

  // ===== Thermal and branch timing =====
  evo_engine::PlottedDistribution<float> lateral_initiation_delay_gdd;
  evo_engine::PlottedDistribution<float> spike_anthesis_offset_gdd;
  evo_engine::PlottedDistribution<float> primary_lateral_branch_probability;
  evo_engine::PlottedDistribution<float> secondary_lateral_branch_probability;

  // ===== Shared =====
  evo_engine::SingleDistribution<float> phyllotaxis_angle{180.0f};
  evo_engine::SingleDistribution<float> branch_azimuth_offset{0.0f};
  evo_engine::SingleDistribution<float> lateral_thickness_ratio{0.6f};

  // -- Secondary branches --
  evo_engine::SingleDistribution<float> secondary_insertion_angle{30.0f};
  evo_engine::SingleDistribution<float> secondary_internode_length{2.0f};
  evo_engine::SingleDistribution<float> secondary_internode_thickness{0.1f};
  evo_engine::SingleDistribution<float> secondary_node_count{1.0f};

  // -- Growth curves --
  evo_engine::Curve2D rachis_elongation_curve;
  evo_engine::Curve2D rachis_thickness_curve;
  evo_engine::Curve2D lateral_elongation_curve;
  evo_engine::Curve2D lateral_thickness_curve;
  evo_engine::Curve2D lateral_angle_development_curve;
  evo_engine::Curve2D pair_proximal_scale_curve;
  evo_engine::Curve2D pair_proximal_angle_curve;
  evo_engine::Curve2D pair_internode_length_curve;
  evo_engine::Curve2D pair_internode_thickness_curve;
  evo_engine::Curve2D pair_internode_angle_curve;
  evo_engine::Curve2D pair_distal_scale_curve;
  evo_engine::Curve2D pair_distal_angle_curve;

  // -- Per-spikelet absolute final age distribution (GDD since spikelet birth) --
  evo_engine::SingleDistribution<float> final_age_gdd{400.0f};

  // -- Dynamic tropism array --
  std::vector<TropismEntry> tropisms;

  // -- GDD milestones --
  evo_engine::SingleDistribution<float> target_gdd{400.0f};
  evo_engine::SingleDistribution<float> gdd_per_day{10.0f};
  evo_engine::SingleDistribution<float> plastochron_gdd{30.0f};
  evo_engine::SingleDistribution<float> anthesis_gdd{200.0f};
  evo_engine::SingleDistribution<float> maturity_gdd{400.0f};

  // -- Initiation clock decoupling --
  evo_engine::SingleDistribution<float> main_axis_plastochron_scale{1.0f};
  evo_engine::SingleDistribution<float> lateral_axis_plastochron_scale{1.0f};
  evo_engine::SingleDistribution<float> lateral_bud_plastochron_scale{1.0f};
  evo_engine::SingleDistribution<float> maturity_initiation_coupling{0.0f};
  evo_engine::SingleDistribution<float> reference_maturity_gdd{400.0f};

  // -- Development pacing controls --
  evo_engine::SingleDistribution<float> branch_angle_relaxation{0.08f};
  evo_engine::SingleDistribution<float> pair_angle_relaxation{1.0f};

  // -- Stage-based tassel unfurling controls (normalized by maturity_gdd) --
  evo_engine::SingleDistribution<float> stage_1_end_t{0.167f};
  evo_engine::SingleDistribution<float> stage_2_end_t{0.50f};
  evo_engine::SingleDistribution<float> stage_3_end_t{0.85f};
  evo_engine::SingleDistribution<float> secondary_ramp_start_t{0.167f};
  evo_engine::SingleDistribution<float> secondary_ramp_end_t{0.50f};
  evo_engine::SingleDistribution<float> mature_droop_start_t{0.85f};
  evo_engine::SingleDistribution<float> mature_droop_strength{0.0f};

  TasselDescriptor();  // applies tuned defaults via ApplyMaizeTasselDefaults()

  // Ported byte-for-byte from MaizeTasselDescriptor::Sample.
  SampledTasselParams Sample(std::mt19937& rng) const {
    SampledTasselParams p;

    // Branch zone (lower rachis).
    p.branch_node_count = std::max(0, static_cast<int>(std::round(SampleDistribution(branch_node_count, rng))));
    p.branch_internode_length = branch_internode_length;
    p.branch_internode_thickness = branch_internode_thickness;
    p.lateral_insertion_angle = lateral_insertion_angle;
    p.lateral_internode_length = lateral_internode_length;
    p.lateral_node_count = lateral_node_count;
    p.peduncle_branch_probability = peduncle_branch_probability;

    // Central spike (upper rachis).
    p.spike_node_count = std::max(0, static_cast<int>(std::round(SampleDistribution(spike_node_count, rng))));
    p.spike_internode_length = spike_internode_length;
    p.spike_internode_thickness = spike_internode_thickness;
    p.spike_zone_branch_probability = spike_zone_branch_probability;

    // Main-rachis pair morphology.
    p.main_pair_proximal_scale_x = main_pair_proximal_scale_x;
    p.main_pair_proximal_scale_y = main_pair_proximal_scale_y;
    p.main_pair_proximal_scale_z = main_pair_proximal_scale_z;
    p.main_pair_proximal_angle = main_pair_proximal_angle;
    p.main_pair_internode_length = main_pair_internode_length;
    p.main_pair_internode_thickness = main_pair_internode_thickness;
    p.main_pair_internode_angle = main_pair_internode_angle;
    p.main_pair_distal_scale_x = main_pair_distal_scale_x;
    p.main_pair_distal_scale_y = main_pair_distal_scale_y;
    p.main_pair_distal_scale_z = main_pair_distal_scale_z;
    p.main_pair_distal_angle = main_pair_distal_angle;

    // Non-main-axis pair morphology.
    p.branch_pair_proximal_scale_x = branch_pair_proximal_scale_x;
    p.branch_pair_proximal_scale_y = branch_pair_proximal_scale_y;
    p.branch_pair_proximal_scale_z = branch_pair_proximal_scale_z;
    p.branch_pair_proximal_angle = branch_pair_proximal_angle;
    p.branch_pair_internode_length = branch_pair_internode_length;
    p.branch_pair_internode_thickness = branch_pair_internode_thickness;
    p.branch_pair_internode_angle = branch_pair_internode_angle;
    p.branch_pair_distal_scale_x = branch_pair_distal_scale_x;
    p.branch_pair_distal_scale_y = branch_pair_distal_scale_y;
    p.branch_pair_distal_scale_z = branch_pair_distal_scale_z;
    p.branch_pair_distal_angle = branch_pair_distal_angle;

    // Branch timing and branch probabilities.
    p.lateral_initiation_delay_gdd = lateral_initiation_delay_gdd;
    p.spike_anthesis_offset_gdd = spike_anthesis_offset_gdd;
    p.primary_lateral_branch_probability = primary_lateral_branch_probability;
    p.secondary_lateral_branch_probability = secondary_lateral_branch_probability;

    // Shared.
    p.phyllotaxis_angle = SampleDistribution(phyllotaxis_angle, rng);
    p.branch_azimuth_offset = branch_azimuth_offset;
    p.lateral_thickness_ratio = std::max(0.01f, SampleDistribution(lateral_thickness_ratio, rng));
    p.final_age_gdd = final_age_gdd;
    p.final_age_gdd.mean = std::max(1.0f, p.final_age_gdd.mean);
    p.final_age_gdd.deviation = std::max(0.0f, p.final_age_gdd.deviation);

    // Secondary branches.
    p.secondary_insertion_angle = SampleDistribution(secondary_insertion_angle, rng);
    p.secondary_internode_length = std::max(0.01f, SampleDistribution(secondary_internode_length, rng));
    p.secondary_internode_thickness = std::max(0.01f, SampleDistribution(secondary_internode_thickness, rng));
    p.secondary_node_count = std::max(0, static_cast<int>(std::round(SampleDistribution(secondary_node_count, rng))));

    // Growth curves.
    p.rachis_elongation_curve = rachis_elongation_curve;
    p.rachis_thickness_curve = rachis_thickness_curve;
    p.lateral_elongation_curve = lateral_elongation_curve;
    p.lateral_thickness_curve = lateral_thickness_curve;
    p.lateral_angle_development_curve = lateral_angle_development_curve;
    p.pair_proximal_scale_curve = pair_proximal_scale_curve;
    p.pair_proximal_angle_curve = pair_proximal_angle_curve;
    p.pair_internode_length_curve = pair_internode_length_curve;
    p.pair_internode_thickness_curve = pair_internode_thickness_curve;
    p.pair_internode_angle_curve = pair_internode_angle_curve;
    p.pair_distal_scale_curve = pair_distal_scale_curve;
    p.pair_distal_angle_curve = pair_distal_angle_curve;

    // Tropisms.
    for (const auto& entry : tropisms) {
      const float usage_chance = std::clamp(entry.usage_chance_percent, 0.0f, 100.0f);
      if (SampleUnit01(rng) * 100.0f > usage_chance) {
        continue;
      }

      SampledTropism st;
      const float dx = SampleDistribution(entry.direction_x, rng);
      const float dy = SampleDistribution(entry.direction_y, rng);
      const float dz = SampleDistribution(entry.direction_z, rng);
      const glm::vec3 dir(dx, dy, dz);
      const float len = glm::length(dir);
      st.direction = (len > 0.001f) ? dir / len : glm::vec3(0.0f, -1.0f, 0.0f);
      st.strength = SampleDistribution(entry.strength, rng);
      st.order_response = entry.order_response;
      p.tropisms.push_back(std::move(st));
    }

    // Thermal timing.
    p.plastochron_gdd = std::max(1.0f, SampleDistribution(plastochron_gdd, rng));
    p.anthesis_gdd = std::max(0.0f, SampleDistribution(anthesis_gdd, rng));
    p.maturity_gdd = std::max(p.anthesis_gdd + 1.0f, SampleDistribution(maturity_gdd, rng));
    p.main_axis_plastochron_scale = std::max(0.1f, SampleDistribution(main_axis_plastochron_scale, rng));
    p.lateral_axis_plastochron_scale = std::max(0.1f, SampleDistribution(lateral_axis_plastochron_scale, rng));
    p.lateral_bud_plastochron_scale = std::max(0.1f, SampleDistribution(lateral_bud_plastochron_scale, rng));
    p.maturity_initiation_coupling = std::max(0.0f, SampleDistribution(maturity_initiation_coupling, rng));
    p.reference_maturity_gdd = std::max(1.0f, SampleDistribution(reference_maturity_gdd, rng));
    p.branch_angle_relaxation = std::clamp(SampleDistribution(branch_angle_relaxation, rng), 0.001f, 1.0f);
    p.pair_angle_relaxation = std::clamp(SampleDistribution(pair_angle_relaxation, rng), 0.001f, 1.0f);
    p.stage_1_end_t = std::clamp(SampleDistribution(stage_1_end_t, rng), 0.0f, 1.0f);
    p.stage_2_end_t = std::clamp(SampleDistribution(stage_2_end_t, rng), std::min(1.0f, p.stage_1_end_t + 0.02f), 1.0f);
    p.stage_3_end_t = std::clamp(SampleDistribution(stage_3_end_t, rng), std::min(1.0f, p.stage_2_end_t + 0.02f), 1.0f);
    p.secondary_ramp_start_t = std::clamp(SampleDistribution(secondary_ramp_start_t, rng), 0.0f, 1.0f);
    p.secondary_ramp_end_t =
        std::clamp(SampleDistribution(secondary_ramp_end_t, rng), std::min(1.0f, p.secondary_ramp_start_t + 0.02f), 1.0f);
    p.mature_droop_start_t = std::clamp(SampleDistribution(mature_droop_start_t, rng), 0.0f, 1.0f);
    p.mature_droop_strength = std::max(0.0f, SampleDistribution(mature_droop_strength, rng));

    return p;
  }
};

// Defined in TasselDescriptorDefaults.cpp (generated). Populates every
// distribution/curve with the tuned maize-tassel genotype.
void ApplyMaizeTasselDefaults(TasselDescriptor& d);

inline TasselDescriptor::TasselDescriptor() {
  ApplyMaizeTasselDefaults(*this);
}

}  // namespace l_system_plugin
