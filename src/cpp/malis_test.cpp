#include "computation_graph/computation/make_affinity.hpp"
#include "computation_graph/computation/get_segmentation.hpp"
#include "computation_graph/computation/zalis.hpp"
#include "computation_graph/computation/constrain_affinity.hpp"
#include "options/options.hpp"
#include "network/parallel/nodes.hpp"

#include <zi/time.hpp>
#include <zi/zargs/zargs.hpp>

using namespace znn::v4;

namespace malis_io {

template<typename F, typename T>
inline cube_p<T> read( std::string const & fname, vec3i const & sz )
{
    FILE* fvol = fopen(fname.c_str(), "r");

    STRONG_ASSERT(fvol);

    auto ret = get_cube<T>(sz);
    F v;

    for ( long_t z = 0; z < sz[0]; ++z )
        for ( long_t y = 0; y < sz[1]; ++y )
            for ( long_t x = 0; x < sz[2]; ++x )
            {
                static_cast<void>(fread(&v, sizeof(F), 1, fvol));
                (*ret)[z][y][x] = static_cast<T>(v);
            }

    fclose(fvol);

    return ret;
}

template<typename F, typename T>
inline bool write( std::string const & fname, cube_p<T> vol )
{
    FILE* fvol = fopen(fname.c_str(), "w");

    STRONG_ASSERT(fvol);

    vec3i sz = size(*vol);
    F v;

    for ( long_t z = 0; z < sz[0]; ++z )
        for ( long_t y = 0; y < sz[1]; ++y )
            for ( long_t x = 0; x < sz[2]; ++x )
            {
                v = static_cast<T>((*vol)[z][y][x]);
                static_cast<void>(fwrite(&v, sizeof(F), 1, fvol));
            }

    fclose(fvol);

    return true;
}

template<typename F, typename T>
inline bool write_vector( std::string const & fname, std::vector<T> vec )
{
    FILE* fvec = fopen(fname.c_str(), "w");

    STRONG_ASSERT(fvec);

    F v;

    for ( auto& elem: vec )
    {
        v = static_cast<T>(elem);
        static_cast<void>(fwrite(&v, sizeof(F), 1, fvec));
    }

    fclose(fvec);

    return true;
}

template<typename F, typename T>
inline bool write_tensor( std::string const & fname,
                          std::vector<cube_p<T>> vols )
{
    FILE* fvol = fopen(fname.c_str(), "w");

    STRONG_ASSERT(fvol);

    F v;

    for ( auto& vol: vols )
    {
        vec3i sz = size(*vol);
        for ( long_t z = 0; z < sz[0]; ++z )
            for ( long_t y = 0; y < sz[1]; ++y )
                for ( long_t x = 0; x < sz[2]; ++x )
                {
                    v = static_cast<T>((*vol)[z][y][x]);
                    static_cast<void>(fwrite(&v, sizeof(F), 1, fvol));
                }
    }

    fclose(fvol);

    return true;
}

bool export_size_info( std::string const & fname,
                       vec3i const & sz, size_t n = 0 )
{
    std::string ssz = fname + ".size";

    FILE* fsz = fopen(ssz.c_str(), "w");

    uint32_t v;

    v = static_cast<uint32_t>(sz[2]); // x
    static_cast<void>(fwrite(&v, sizeof(uint32_t), 1, fsz));

    v = static_cast<uint32_t>(sz[1]); // y
    static_cast<void>(fwrite(&v, sizeof(uint32_t), 1, fsz));

    v = static_cast<uint32_t>(sz[0]); // z
    static_cast<void>(fwrite(&v, sizeof(uint32_t), 1, fsz));

    if ( n )
    {
        v = static_cast<uint32_t>(n);
        static_cast<void>(fwrite(&v, sizeof(uint32_t), 1, fsz));
    }

    fclose(fsz);

    return true;
}

}

int main(int argc, char** argv)
{
    // ---------------------------------------------------------------
    // option parsing
    // ---------------------------------------------------------------
    options op;

    std::string fname(argv[1]);

    parse_option_file(op, fname);

    // input volume size
    vec3i s = op.require_as<ovec3i>("size");

    // I/O paths
    std::string bfname = op.require_as<std::string>("bmap");
    std::string lfname = op.require_as<std::string>("lbl");
    std::string ofname = op.require_as<std::string>("out");

    // high & low threshold
    real high = op.optional_as<real>("high","1");
    real low  = op.optional_as<real>("low","0");

    // zalis phase
    zalis_phase  phs;
    std::string sphs = op.optional_as<std::string>("phase","BOTH");
    if ( sphs == "BOTH" )
    {
        phs = zalis_phase::BOTH;
    }
    else if ( sphs == "MERGER" )
    {
        phs = zalis_phase::MERGER;
    }
    else if ( sphs == "SPLITTER" )
    {
        phs = zalis_phase::SPLITTER;
    }
    else
    {
        throw std::logic_error(HERE() + "unknown zalis_phase: " + sphs);
    }

    // constrained
    bool is_constrained = op.optional_as<bool>("constrain","0");

    // fraction normalize
    bool is_frac_norm = op.optional_as<bool>("frac_norm","0");

    // debug print
    bool debug_print = op.optional_as<bool>("debug_print","0");

    // load input
    auto bmap = malis_io::read<double,real>(bfname,s);
    auto lbl  = malis_io::read<double,int>(lfname,s);

    if ( debug_print )
    {
        std::cout << "\n[bmap]\n" << *bmap << std::endl;
        std::cout << "\n[lbl]\n"  << *lbl << std::endl;
    }

    // ---------------------------------------------------------------
    // make affinity
    // ---------------------------------------------------------------
    zi::wall_timer wt;
    wt.reset();

    auto affs      = make_affinity( *bmap );
    auto true_affs = make_affinity( *lbl );

    std::cout << "\n[make_affinity] done, elapsed: "
              << wt.elapsed<double>() << " secs" << std::endl;

    if ( debug_print )
    {
        std::cout << "\n[affs]" << std::endl;
        for ( auto& aff: affs )
        {
            std::cout << *aff << "\n\n";
        }

        std::cout << "\n[true_affs]" << std::endl;
        for ( auto& aff: true_affs )
        {
            std::cout << *aff << "\n\n";
        }
    }

    // ---------------------------------------------------------------
    // constrain affinity
    // ---------------------------------------------------------------
    wt.reset();

    if ( is_constrained )
    {
        auto constrained = constrain_affinity(true_affs, affs, phs);
        affs.clear();
        affs = constrained;

        std::cout << "\n[constrain_affinity] done, elapsed: "
                  << wt.elapsed<double>() << " secs" << std::endl;

        if ( debug_print )
        {
            std::cout << "\n[constrained affs]" << std::endl;
            for ( auto& aff: affs )
            {
                std::cout << *aff << "\n\n";
            }
        }
    }

    // ---------------------------------------------------------------
    // ZALIS weight
    // ---------------------------------------------------------------
    wt.reset();

    auto weight = zalis(true_affs, affs, high, low, is_frac_norm);

    std::cout << "\n[zalis] done, elapsed: "
              << wt.elapsed<double>() << std::endl;

    if ( debug_print )
    {
        std::cout << "\n[merger weight]" << std::endl;
        for ( auto& w: weight.merger )
        {
            std::cout << *w << "\n\n";
        }

        std::cout << "\n[splitter weight]" << std::endl;
        for ( auto& w: weight.splitter )
        {
            std::cout << *w << "\n\n";
        }
    }

    // ---------------------------------------------------------------
    // save results
    // ---------------------------------------------------------------
    wt.reset();

    // write affinity
    malis_io::write_tensor<double,real>(ofname + ".affs",affs);
    malis_io::export_size_info(ofname + ".affs",s,affs.size());

    // write merger weight
    malis_io::write_tensor<double,real>(ofname + ".merger",weight.merger);
    malis_io::export_size_info(ofname + ".merger",s,weight.merger.size());

    // write splitter weight
    malis_io::write_tensor<double,real>(ofname + ".splitter",weight.splitter);
    malis_io::export_size_info(ofname + ".splitter",s,weight.splitter.size());

#if defined( DEBUG )
    // write watershed snapshots
    malis_io::write_tensor<double,int>(ofname + ".snapshots",weight.ws_snapshots);
    malis_io::export_size_info(ofname + ".snapshots",s,weight.ws_snapshots.size());

    // write snapshot timestamp
    malis_io::write_vector<double,int>(ofname + ".snapshots_time",weight.ws_timestamp);

    // write timestamp
    malis_io::write_tensor<double,int>(ofname + ".timestamp",weight.timestamp);
    malis_io::export_size_info(ofname + ".timestamp",s,weight.timestamp.size());
#endif

    std::cout << "\n[save] done, elapsed: "
              << wt.elapsed<double>() << '\n' << std::endl;
}