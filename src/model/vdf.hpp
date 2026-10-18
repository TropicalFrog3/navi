#pragma once

#include <string>
#include <vector>

namespace VDF
{
	struct Node
	{
		std::string key;
		std::string value;
		std::vector<Node> children;

		bool isSection() const
		{
			return !children.empty() || value.empty();
		}

		const Node* getChild(const std::string& childKey) const;
		std::string getString(const std::string& childKey, const std::string& defaultVal = "") const;
		std::vector<const Node*> getChildren(const std::string& childKey) const;
	};

	bool parse(const std::string& content, Node& root);
	bool parseFile(const std::string& filepath, Node& root);
}
