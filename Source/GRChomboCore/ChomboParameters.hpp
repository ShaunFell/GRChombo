/* GRChombo
 * Copyright 2012 The GRChombo collaboration.
 * Please refer to LICENSE in GRChombo's root directory.
 */

#ifndef CHOMBOPARAMETERS_HPP_
#define CHOMBOPARAMETERS_HPP_

// General includes
#include "BoundaryConditions.hpp"
#include "GRParmParse.hpp"
#include "UserVariables.hpp"
#include "VariableType.hpp"
#include <algorithm>

class ChomboParameters
{
  public:
    ChomboParameters(GRParmParse &pp) { read_params(pp); }

    void read_params(GRParmParse &pp)
    {
        pp.load("verbosity", verbosity, 0);
        // Grid setup
        pp.load("regrid_threshold", regrid_threshold, 0.5);
        pp.load("num_ghosts", num_ghosts, 3);
        pp.load("tag_buffer_size", tag_buffer_size, 3);
        pp.load("dt_multiplier", dt_multiplier, 0.25);
        pp.load("fill_ratio", fill_ratio, 0.7);

        // Periodicity and boundaries
        pp.load("isPeriodic", isPeriodic, {true, true, true});
        int bc = BoundaryConditions::STATIC_BC;
        pp.load("hi_boundary", boundary_params.hi_boundary, {bc, bc, bc});
        pp.load("lo_boundary", boundary_params.lo_boundary, {bc, bc, bc});
        // set defaults, then override them where appropriate
        boundary_params.vars_parity.fill(BoundaryConditions::EVEN);
        boundary_params.vars_asymptotic_values.fill(0.0);
        boundary_params.is_periodic.fill(true);
        nonperiodic_boundaries_exist = false;
        symmetric_boundaries_exist = false;
        FOR1(idir)
        {
            if (isPeriodic[idir] == false)
            {
                nonperiodic_boundaries_exist = true;
                boundary_params.is_periodic[idir] = false;

                // read in relevent params - note that no defaults are set so as
                // to force the user to specify them where the relevant BCs are
                // selected
                if ((boundary_params.hi_boundary[idir] ==
                     BoundaryConditions::REFLECTIVE_BC) ||
                    (boundary_params.lo_boundary[idir] ==
                     BoundaryConditions::REFLECTIVE_BC))
                {
                    symmetric_boundaries_exist = true;
                    pp.load("vars_parity", boundary_params.vars_parity);
                }
                if ((boundary_params.hi_boundary[idir] ==
                     BoundaryConditions::SOMMERFELD_BC) ||
                    (boundary_params.lo_boundary[idir] ==
                     BoundaryConditions::SOMMERFELD_BC))
                {
                    pp.load("vars_asymptotic_values",
                            boundary_params.vars_asymptotic_values);
                }
            }
        }
        if (nonperiodic_boundaries_exist)
        {
            // write out boundary conditions where non periodic - useful for
            // debug
            BoundaryConditions::write_boundary_conditions(boundary_params);
        }

        // Grid N
        std::array<int, CH_SPACEDIM> Ni_full;
        std::array<int, CH_SPACEDIM> Ni;
        ivN = IntVect::Unit;

        // cannot contain both
        CH_assert(!(pp.contains("N_full") && pp.contains("N")));

        int N_full = -1;
        int N = -1;
        if (pp.contains("N_full"))
            pp.load("N_full", N_full);
        else if (pp.contains("N"))
            pp.load("N", N);

        // read all options (N, N_full, Ni_full and Ni) and then choose
        // accordingly
        FOR1(dir)
        {
            std::string name = ("N" + std::to_string(dir + 1));
            std::string name_full = ("N" + std::to_string(dir + 1) + "_full");
            Ni_full[dir] = -1;
            Ni[dir] = -1;

            // only one of them exists - this passes if none of the 4 exist, but
            // that is asserted below
            CH_assert(
                ((N_full > 0 || N > 0) && !pp.contains(name.c_str()) &&
                 !pp.contains(name_full.c_str())) ||
                ((N_full < 0 && N < 0) && !(pp.contains(name.c_str()) &&
                                            pp.contains(name_full.c_str()))));

            if (N_full < 0 && N < 0)
            {
                if (pp.contains(name_full.c_str()))
                    pp.load(name_full.c_str(), Ni_full[dir]);
                else
                    pp.load(name.c_str(), Ni[dir]);
            }
            CH_assert(N > 0 || N_full > 0 || Ni[dir] > 0 ||
                      Ni_full[dir] > 0); // sanity check

            if (N_full > 0)
                Ni_full[dir] = N_full;
            else if (N > 0)
                Ni[dir] = N;

            if (Ni[dir] > 0)
                Ni_full[dir] = Ni[dir];
            else
            {
                if (boundary_params.lo_boundary[dir] ==
                        BoundaryConditions::REFLECTIVE_BC ||
                    boundary_params.hi_boundary[dir] ==
                        BoundaryConditions::REFLECTIVE_BC)
                {
                    CH_assert(Ni_full[dir] % 2 == 0); // Ni_full is even
                    Ni[dir] = Ni_full[dir] / 2;
                }
                else
                    Ni[dir] = Ni_full[dir];
            }
            ivN[dir] = Ni[dir] - 1;
        }
        int max_N_full = *std::max_element(Ni_full.begin(), Ni_full.end());
        int max_N = ivN.max() + 1;

        // Grid L
        // cannot contain both
        CH_assert(!(pp.contains("L_full") && pp.contains("L")));

        double L_full = -1.;
        if (pp.contains("L_full"))
            pp.load("L_full", L_full);
        else
            pp.load("L", L, 1.0);

        if (L_full > 0.)
            // necessary for some reflective BC cases, as 'L' is the
            // length of the longest side of the box
            L = (L_full * max_N) / max_N_full;

        coarsest_dx = L / max_N;

        // extraction params
        dx.fill(coarsest_dx);
        origin.fill(coarsest_dx / 2.0);

        // Grid center
        // now that L is surely set, get center
        pp.load("center", center,
                {0.5 * Ni[0] * coarsest_dx, 0.5 * Ni[1] * coarsest_dx,
                 0.5 * Ni[2] * coarsest_dx}); // default to center

        FOR1(idir)
        {
            if ((boundary_params.lo_boundary[idir] ==
                 BoundaryConditions::REFLECTIVE_BC))
                center[idir] = 0.;
            else if ((boundary_params.hi_boundary[idir] ==
                      BoundaryConditions::REFLECTIVE_BC))
                center[idir] = coarsest_dx * Ni[idir];
        }
        pout() << "Center has been set to: ";
        FOR1(idir) { pout() << center[idir] << " "; }
        pout() << endl;

        // Misc
        pp.load("ignore_checkpoint_name_mismatch",
                ignore_checkpoint_name_mismatch, false);

        pp.load("max_level", max_level, 0);
        // the reference ratio is hard coded to 2
        // in principle it can be set to other values, but this is
        // not recommended since we do not test GRChombo with other
        // refinement ratios - use other values at your own risk
        ref_ratios.resize(max_level + 1);
        ref_ratios.assign(2);
        pp.getarr("regrid_interval", regrid_interval, 0, max_level + 1);

        // time stepping outputs and regrid data
        pp.load("checkpoint_interval", checkpoint_interval, 1);
        pp.load("chk_prefix", checkpoint_prefix);
        pp.load("plot_interval", plot_interval, 0);
        pp.load("plot_prefix", plot_prefix);
        pp.load("stop_time", stop_time, 1.0);
        pp.load("max_steps", max_steps, 1000000);
        pp.load("write_plot_ghosts", write_plot_ghosts, false);

        // load vars to write to plot files
        pp.load("num_plot_vars", num_plot_vars, 0);
        std::vector<std::string> plot_var_names(num_plot_vars, "");
        pp.load("plot_vars", plot_var_names, num_plot_vars, plot_var_names);
        for (std::string var_name : plot_var_names)
        {
            // first assume plot_var is a normal evolution var
            int var = UserVariables::variable_name_to_enum(var_name);
            VariableType var_type = VariableType::evolution;
            if (var < 0)
            {
                // if not an evolution var check if it's a diagnostic var
                var = DiagnosticVariables::variable_name_to_enum(var_name);
                if (var < 0)
                {
                    // it's neither :(
                    pout() << "Variable with name " << var_name
                           << " not found.\n";
                }
                else
                {
                    var_type = VariableType::diagnostic;
                }
            }
            if (var >= 0)
            {
                plot_vars.emplace_back(var, var_type);
            }
        }
        num_plot_vars = plot_vars.size();

        // alias the weird chombo names to something more descriptive
        // for these box params, and default to some reasonable values
        if (pp.contains("max_grid_size"))
        {
            pp.load("max_grid_size", max_grid_size);
        }
        else
        {
            pp.load("max_box_size", max_grid_size, 64);
        }
        if (pp.contains("block_factor"))
        {
            pp.load("block_factor", block_factor);
        }
        else
        {
            pp.load("min_box_size", block_factor, 8);
        }
    }

    // General parameters
    int verbosity;
    double L;                               // Physical sidelength of the grid
    std::array<double, CH_SPACEDIM> center; // grid center
    IntVect ivN;                 // The number of grid cells in each dimension
    double coarsest_dx;          // The coarsest resolution
    int max_level;               // the max number of regriddings to do
    int num_ghosts;              // must be at least 3 for KO dissipation
    int tag_buffer_size;         // Amount the tagged region is grown by
    Vector<int> ref_ratios;      // ref ratios between levels
    Vector<int> regrid_interval; // steps between regrid at each level
    int max_steps;
    bool ignore_checkpoint_name_mismatch;   // ignore mismatch of variable names
                                            // between restart file and program
    double dt_multiplier, stop_time;        // The Courant factor and stop time
    int checkpoint_interval, plot_interval; // Steps between outputs
    int max_grid_size, block_factor;        // max and min box sizes
    double fill_ratio; // determines how fussy the regridding is about tags
    std::string checkpoint_prefix, plot_prefix; // naming of files
    bool write_plot_ghosts;
    int num_plot_vars;
    std::vector<std::pair<int, VariableType>>
        plot_vars; // vars to write to plot file

    std::array<double, CH_SPACEDIM> origin,
        dx; // location of coarsest origin and dx

    // Boundary conditions
    std::array<bool, CH_SPACEDIM> isPeriodic;     // periodicity
    BoundaryConditions::params_t boundary_params; // set boundaries in each dir
    bool nonperiodic_boundaries_exist;
    bool symmetric_boundaries_exist;

    // For tagging
    double regrid_threshold;
};

#endif /* CHOMBOPARAMETERS_HPP_ */
