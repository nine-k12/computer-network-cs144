#ifndef SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
#define SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH

#include <cstdint>
#include <string>
#include <set>

#include "byte_stream.hh"


struct Node{
	size_t index;
	std::string data;
	Node(const uint64_t id, const std::string &dat):index(id), data(dat){}
	bool operator<(const Node& other) const {return index < other.index;}
};

class StreamReassembler{
  private:
		size_t _capacity;
		size_t _head_index;
		size_t _unassembled_bytes;
		std::set<Node> _waitting_members;
		ByteStream  _output;
		bool _eof;

		bool _merge_substring(Node &node1, const Node& node2);

  public:
		StreamReassembler(const size_t capacity);

		void push_substring(const std::string &data, const size_t index, const bool eof);
		ByteStream &stream_out();
		const ByteStream &stream_out() const;
		size_t unassembled_bytes() const;
		size_t head_index() const;
		bool empty() const;
};

#endif