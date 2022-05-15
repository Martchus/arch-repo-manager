#ifndef LIBPKG_DATA_MRU_LIST_H
#define LIBPKG_DATA_MRU_LIST_H

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

namespace LibPkg {

template <typename Item> class MostRecentUseList {
    using ItemList = boost::multi_index::multi_index_container<Item,
        boost::multi_index::indexed_by<boost::multi_index::sequenced<>, boost::multi_index::hashed_unique<boost::multi_index::identity<Item>>>>;

public:
    using item_type = Item;
    using iterator = typename ItemList::iterator;

    explicit MostRecentUseList(std::size_t limit = 1000)
        : m_limit(limit)
    {
    }

    void insert(const item_type &item)
    {
        auto [i, newItem] = std::pair<iterator, bool>(m_itemList.push_front(item));
        if (!newItem) {
            m_itemList.relocate(m_itemList.begin(), i);
        } else if (m_itemList.size() > m_limit) {
            m_itemList.pop_back();
        }
    }

    iterator begin()
    {
        return m_itemList.begin();
    }
    iterator end()
    {
        return m_itemList.end();
    }

private:
    ItemList m_itemList;
    std::size_t m_limit;
};

} // namespace LibPkg

#endif // LIBPKG_DATA_MRU_LIST_H
