/**
 * @file  vdf.cpp
 * @brief Valve KeyValues (VDF) text parser and serializer
 */
#include "vdf.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

namespace VDF
{
	const Node* Node::getChild(const std::string& childKey) const
	{
		for (const auto& child : children)
		{
			if (child.key == childKey)
			{
				return &child;
			}
		}
		return nullptr;
	}

	std::string Node::getString(const std::string& childKey, const std::string& defaultVal) const
	{
		const auto* child = getChild(childKey);
		return (child && !child->value.empty()) ? child->value : defaultVal;
	}

	std::vector<const Node*> Node::getChildren(const std::string& childKey) const
	{
		std::vector<const Node*> result;
		for (const auto& child : children)
		{
			if (child.key == childKey)
			{
				result.push_back(&child);
			}
		}
		return result;
	}

	namespace
	{
		struct Parser
		{
			const std::string& src;
			size_t pos = 0;

			void skipWhitespaceAndComments()
			{
				while (pos < src.size())
				{
					char c = src[pos];
					if (std::isspace(static_cast<unsigned char>(c)))
					{
						pos++;
					}
					else if (c == '/' && pos + 1 < src.size() && src[pos + 1] == '/')
					{
						pos += 2;
						while (pos < src.size() && src[pos] != '\n')
						{
							pos++;
						}
					}
					else
					{
						break;
					}
				}
			}

			bool hasMore()
			{
				skipWhitespaceAndComments();
				return pos < src.size();
			}

			bool nextToken(std::string& token, bool& isBraceOpen, bool& isBraceClose)
			{
				isBraceOpen = false;
				isBraceClose = false;
				token.clear();

				skipWhitespaceAndComments();
				if (pos >= src.size())
				{
					return false;
				}

				char c = src[pos];
				if (c == '{')
				{
					isBraceOpen = true;
					pos++;
					return true;
				}
				if (c == '}')
				{
					isBraceClose = true;
					pos++;
					return true;
				}

				if (c == '"')
				{
					pos++; // Skip opening quote
					while (pos < src.size())
					{
						char ch = src[pos];
						if (ch == '\\' && pos + 1 < src.size())
						{
							pos++;
							token += src[pos++];
						}
						else if (ch == '"')
						{
							pos++; // Skip closing quote
							break;
						}
						else
						{
							token += ch;
							pos++;
						}
					}
					return true;
				}

				// Bare unquoted word
				while (pos < src.size())
				{
					char ch = src[pos];
					if (std::isspace(static_cast<unsigned char>(ch)) || ch == '{' || ch == '}' || ch == '"')
					{
						break;
					}
					if (ch == '/' && pos + 1 < src.size() && src[pos + 1] == '/')
					{
						break;
					}
					token += ch;
					pos++;
				}
				return !token.empty();
			}

			bool parseNodeList(std::vector<Node>& list)
			{
				while (hasMore())
				{
					std::string key;
					bool isOpen = false, isClose = false;
					if (!nextToken(key, isOpen, isClose))
					{
						return true;
					}
					if (isClose)
					{
						return true;
					}
					if (isOpen)
					{
						continue;
					}

					skipWhitespaceAndComments();
					if (pos >= src.size())
					{
						list.push_back(Node{std::move(key), "", {}});
						return true;
					}

					char c = src[pos];
					if (c == '{')
					{
						pos++; // Consume '{'
						Node secNode{std::move(key), "", {}};
						parseNodeList(secNode.children);
						list.push_back(std::move(secNode));
					}
					else
					{
						std::string val;
						bool vOpen = false, vClose = false;
						if (nextToken(val, vOpen, vClose))
						{
							if (vOpen)
							{
								Node secNode{std::move(key), "", {}};
								parseNodeList(secNode.children);
								list.push_back(std::move(secNode));
							}
							else
							{
								list.push_back(Node{std::move(key), std::move(val), {}});
							}
						}
					}
				}
				return true;
			}
		};
	}

	bool parse(const std::string& content, Node& root)
	{
		Parser p{content};
		root = Node{"", "", {}};
		return p.parseNodeList(root.children);
	}

	bool parseFile(const std::string& filepath, Node& root)
	{
		std::ifstream file(filepath);
		if (!file.is_open())
		{
			return false;
		}

		std::stringstream buffer;
		buffer << file.rdbuf();
		return parse(buffer.str(), root);
	}
}
