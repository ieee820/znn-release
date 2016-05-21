//
// Copyright (C) 2012-2015  Aleksandar Zlateski <zlateski@mit.edu>
// ---------------------------------------------------------------
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#pragma once

#include "edge.hpp"
#include "edges_fwd.hpp"
#include "nodes.hpp"

#include "../../convolution/convolution.hpp"
#include "../filter.hpp"

namespace znn { namespace v4 { namespace parallel_network {

class filter_ds_edge: public edge
{
private:
    vec3i    filter_stride;
    vec3i    repeat_;
    filter & filter_;
    bool     deconv_;
    bool     shared_;

    ccube_p<real> last_input;

    task_manager::task_handle pending_ = 0;

private:
    inline cube_p<real> convolve_forward( cube<real> const & a,
                                          cube<real> const & b,
                                          vec3i const & s )
    {
        return deconv_ ? convolve_sparse_inverse(a,b,s)
                       : convolve_sparse(a,b,s);

    }

    inline cube_p<real> convolve_backward( cube<real> const & a,
                                           cube<real> const & b,
                                           vec3i const & s )
    {
        return deconv_ ? convolve_sparse(a,b,s)
                       : convolve_sparse_inverse(a,b,s);
    }

private:
    void do_forward( ccube_p<real> const & f )
    {
        ZI_ASSERT(enabled_);

        last_input = f;

        out_nodes->forward(out_num,
            convolve_forward(*f, filter_.W(), filter_stride));
    }

    void do_update( ccube_p<real> const & g )
    {
        ZI_ASSERT(enabled_);

        auto dEdW =
            deconv_ ? convolve_sparse_flipped(*g, *last_input, filter_stride)
                    : convolve_sparse_flipped(*last_input, *g, filter_stride);

        filter_.update(*dEdW, patch_sz_);
        flatten(filter_.W(), repeat_);
    }

public:
    filter_ds_edge( nodes * in,
                    size_t inn,
                    nodes * out,
                    size_t outn,
                    task_manager & tm,
                    vec3i const & stride,
                    vec3i const & repeat,
                    filter & f,
                    bool deconv = false,
                    bool shared = false )
        : edge(in,inn,out,outn,tm)
        , filter_stride(stride)
        , repeat_(repeat)
        , filter_(f)
        , deconv_(deconv)
        , shared_(shared)
    {
        in->attach_out_edge(inn,this);
        out->attach_in_edge(outn,this);
        flatten(filter_.W(), repeat_);
    }

    void forward( ccube_p<real> const & f ) override
    {
        if ( !enabled_ ) return;

        manager.require_done( pending_, &filter_ds_edge::do_forward, this, f );
    }

    void backward( ccube_p<real> const & g ) override
    {
        if ( !enabled_ ) return;

        ZI_ASSERT(last_input);

        in_nodes->backward(in_num,
                       convolve_backward(*g, filter_.W(), filter_stride));

        if ( shared_ )
        {
            do_update(g); // immediate update
        }
        else
        {
            pending_ = manager.schedule_unprivileged(&filter_ds_edge::do_update,
                                                     this, g);
        }
    }

    void zap(edges* e) override
    {
        // guard gg(m);
        manager.require_done(pending_,&edges::edge_zapped,e);
        //e->edge_zapped();
    }
};

}}} // namespace znn::v4::parallel_network