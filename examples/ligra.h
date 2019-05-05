#include "sequence.h"
#include "get_time.h"
#include "strings/string_basics.h"

namespace ligra {
  
  using namespace pbbs;

  using vertex = uint;
  using edge_index = size_t;

  // **************************************************************
  //    sparse compressed row representation of a graph
  // **************************************************************

  struct graph {
    using NGH = range<vertex*>;
    sequence<edge_index> offsets;
    sequence<vertex> edges;
    size_t num_vertices() const {return offsets.size();}
    size_t num_edges() const {return edges.size();}
    NGH operator[] (const size_t i) const {
      size_t end = (i==num_vertices()-1) ? num_edges() :offsets[i+1];
      return edges.slice(offsets[i], end);
    }
  };

  // **************************************************************
  //    vertex_subset
  // **************************************************************

  struct vertex_subset {
    bool is_dense;
    sequence<bool> flags;
    sequence<vertex> indices;
    vertex_subset(vertex v) : is_dense(false), indices(singleton(v)) {}
    vertex_subset(sequence<vertex> indices)
      : is_dense(false), indices(std::move(indices)) {}
    vertex_subset(sequence<bool> flags)
      : is_dense(true), flags(std::move(flags)) {}
    size_t size() const {
      return is_dense ? count(flags, true) : indices.size();}
    bool is_empty() {return size() == 0;}
    sequence<bool> get_flags(size_t n) const {
      if (is_dense) return flags;
      sequence<bool> r(n, false);
      parallel_for (0, indices.size(), [&] (size_t i) {
	  r[indices[i]] = true;});
      return r;
    }
    sequence<vertex> get_indices() const {
      if (!is_dense) return indices;
      return pack_index<vertex>(flags);
    }
  };

  // **************************************************************
  //    read a graph
  // **************************************************************

  graph read_graph(char* filename) {
    sequence<char> str = char_range_from_file(filename);
    auto is_space = [&] (char a) {return a == ' ' || a == '\n';};
    auto words = tokenize(str, is_space);
    size_t n = atol(words[1]);
    size_t m = atol(words[2]);
    if (3 + n + m != words.size()) abort();
    graph g;
    g.offsets = map(words.slice(3,3+n), [&] (auto &s) {
	return (edge_index) atol(s);});
    g.edges = map(words.slice(3+n,3+n+m), [&] (auto &s)  {
	return (vertex) atol(s);});
    return g;
  }

  // **************************************************************
  //    edge_map
  // **************************************************************

  size_t sparse_dense_ratio = 10;
  
  template <typename mapper>
  vertex_subset edge_map(graph const &g, vertex_subset const &vs, mapper &m) {

    auto edge_map_sparse = [&] (sequence<vertex> const &idx) {
      size_t n = g.num_vertices();
      //cout << "sparse: " << idx.size() << endl;

      sequence<vertex> offsets(idx.size(), [&] (size_t i) {
	  return g[idx[i]].size();});
    
      // Find offsets to write the next frontier for each v in this frontier
      size_t total = pbbs::scan_inplace(offsets.slice(), addm<vertex>());
      pbbs::sequence<vertex> next(total);

      parallel_for(0, idx.size(), [&] (size_t i) {
	  auto v = idx[i];
	  auto ngh = g[v];
	  auto o = offsets[i];
	  parallel_for(0, ngh.size(), [&] (size_t j) {
	      next[o + j] = (m.cond(ngh[j]) &&
			     m.updateAtomic(v, ngh[j])) ? ngh[j] : n;
	    }, 200);
	});

      auto r = filter(next, [&] (vertex i) {return i < n;});
      return vertex_subset(std::move(r));
    };

    auto edge_map_dense = [&] (sequence<bool> const &flags) {
      //cout << "dense: " << count(flags,true) << endl;

      sequence<bool> out_flags(flags.size(), [&] (size_t d) {
	  auto in_nghs = g[d];
	  bool r = false;
	  for (size_t i=0; i < in_nghs.size(); i++) {
	    if (!m.cond(d)) break;
	    auto s = in_nghs[i];
	    if (flags[s] && m.update(s, d)) r = true;
	  }
	  return r;
	});

      return vertex_subset(std::move(out_flags));
    };

    if (vs.size() > g.num_vertices()/sparse_dense_ratio)
      if (vs.is_dense) return edge_map_dense(vs.flags);
      else return edge_map_dense(vs.get_flags(g.num_vertices()));
    else
      if (vs.is_dense) return edge_map_sparse(vs.get_indices());
      else return edge_map_sparse(vs.indices);
  }
}
