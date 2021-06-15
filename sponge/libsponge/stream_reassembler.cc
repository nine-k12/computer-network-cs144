#include "stream_reassembler.hh"

bool StreamReassembler::_merge_substring(Node& node1, const Node& node2){
	size_t len1 = node1.index + node1.data.size();
	size_t len2 = node2.index + node2.data.size();
	if(node1.index > len2 || node2.index > len1)
		return false;

	Node low = node2, high = node1;
	if(node1 < node2){
		low = node1;
		high = node2;
	}

	if(low.index+low.data.size() <= high.index + high.data.size())
		low.data = low.data.substr(0, high.index-low.index) + high.data;

	node1 = low;
	return true;
}

StreamReassembler::StreamReassembler(const size_t capacity):_capacity(capacity), _head_index(0),
	_unassembled_bytes(0), _waitting_members(), _output(capacity), _eof(false) {}

// _output is a buffer which is used for storing datagram, and using a tree
// structure to make those datagrams ordered
void StreamReassembler::push_substring(const std::string &data, const size_t index, const bool eof){
	_eof = _eof | eof;
	if(data.size() > _capacity)
		_eof = false;
	if(data.empty() || index+data.size() <= _head_index){
		if(_eof)
			_output.end_input();
		return;
	}
	
	size_t win_hb = _head_index + _output.remaining_capacity();
	// crop datagram to fit current usable window
	Node node(index, data);
	if(node.index < _head_index){
		node.data = node.data.substr(_head_index-node.index);
		node.index = _head_index;
	}
	if(node.index + node.data.size() > win_hb){
		node.data = node.data.substr(0, win_hb-node.index);
		_eof = false;
	}
	
	// merge node <-- 
	auto it = _waitting_members.lower_bound(node);
	while(it != _waitting_members.begin()){
		if(it == _waitting_members.end())
			it--;
		bool flag = _merge_substring(node, *it);
		if(flag){
			_unassembled_bytes -= it->data.size();
			if(it != _waitting_members.begin()){
				_waitting_members.erase(it--);
			}
			else{
				_waitting_members.erase(it);
				break;
			}
		}
		else break;	
	}

	// merge node -->
	it = _waitting_members.lower_bound(node);
	while(it != _waitting_members.end()){
		bool flag = _merge_substring(node, *it);
		if(flag){
			_unassembled_bytes -= it->data.size();
			_waitting_members.erase(it++);
		}
		else break;
	}

	if(node.index <= _head_index){
		std::string write_data = node.data.substr(_head_index-node.index);
		size_t write_cnt = _output.write(write_data);
		if(write_cnt == node.data.size() && eof)
			_output.end_input();
		_head_index += write_cnt;
	}
	else{
		_waitting_members.insert(node);
		_unassembled_bytes += node.data.size();
	}

	if(empty() && _eof)
		_output.end_input();
	return;
}

ByteStream &StreamReassembler::stream_out(){
	return _output;
}

const ByteStream &StreamReassembler::stream_out() const{
	return _output;
}

size_t StreamReassembler::unassembled_bytes() const{
	return _unassembled_bytes;
}

size_t StreamReassembler::head_index() const{
	return _head_index;
}

bool StreamReassembler::empty() const{
	return _unassembled_bytes == 0;
}

