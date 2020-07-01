#pragma once
#include <memory>

template <typename T>
class LinkedList
{
public:
	LinkedList();
	~LinkedList();

	void Push(const T& data);
	void Pop();
	void Clear();
	T GetHeadData() const;

	bool IsEmpty() const;
	size_t GetSize() const;

private:
	struct Node
	{
		Node(const T& value, const std::shared_ptr<Node>& node)
			: data(value)
			, next(node)
		{
		}

		T data;
		std::shared_ptr<Node> next = nullptr;
	};

	size_t m_size;
	std::shared_ptr<Node> m_head;
};

template <typename T>
LinkedList<T>::LinkedList::LinkedList()
	: m_size(0)
	, m_head(nullptr)
{
}

template <typename T>
LinkedList<T>::LinkedList::~LinkedList()
{
	Clear();
}

template <typename T>
void LinkedList<T>::Push(const T& data)
{
	m_head = std::make_shared<Node>(data, m_head);
	++m_size;
}

template <typename T>
void LinkedList<T>::Pop()
{
	if (!IsEmpty())
	{
		m_head = m_head->next;
		--m_size;
	}
}

template <typename T>
void LinkedList<T>::Clear()
{
	while (!IsEmpty())
	{
		Pop();
	}
}

template <typename T>
T LinkedList<T>::GetHeadData() const
{
	if (IsEmpty())
	{
		throw std::exception("Empty list");
	}
	return m_head->data;
}

template <typename T>
bool LinkedList<T>::IsEmpty() const
{
	return m_size == 0;
}

template <typename T>
size_t LinkedList<T>::GetSize() const
{
	return m_size;
}