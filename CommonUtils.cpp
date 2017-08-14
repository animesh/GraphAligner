#include "CommonUtils.h"
#include "stream.hpp"

namespace CommonUtils
{

	void mergeGraphs(vg::Graph& graph, const vg::Graph& part)
	{
		for (size_t i = 0; i < part.node_size(); i++)
		{
			auto node = graph.add_node();
			node->set_id(part.node(i).id());
			node->set_sequence(part.node(i).sequence());
			node->set_name(part.node(i).name());
		}
		for (size_t i = 0; i < part.edge_size(); i++)
		{
			auto edge = graph.add_edge();
			edge->set_from(part.edge(i).from());
			edge->set_to(part.edge(i).to());
			edge->set_from_start(part.edge(i).from_start());
			edge->set_to_end(part.edge(i).to_end());
			edge->set_overlap(part.edge(i).overlap());
		}
	}

	vg::Graph LoadVGGraph(std::string filename)
	{
		vg::Graph result;
		std::ifstream graphfile { filename, std::ios::in | std::ios::binary };
		std::function<void(vg::Graph&)> lambda = [&result](vg::Graph& g) {
			mergeGraphs(result, g);
		};
		stream::for_each(graphfile, lambda);
		return result;
	}


	std::string ReverseComplement(std::string str)
	{
		std::string result;
		result.reserve(str.size());
		for (int i = str.size()-1; i >= 0; i--)
		{
			switch (str[i])
			{
				case 'A':
				case 'a':
				result += 'T';
				break;
				case 'C':
				case 'c':
				result += 'G';
				break;
				case 'T':
				case 't':
				result += 'A';
				break;
				case 'G':
				case 'g':
				result += 'C';
				break;
				case 'N':
				case 'n':
				result += 'N';
				break;
			}
		}
		return result;
	}

}