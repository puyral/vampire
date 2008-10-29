/**
 * @file Map.hpp
 * Defines class Map<Key,Val> of arbitrary maps and its companion.
 */

#ifndef __Map__
#define __Map__

#include <cstdlib>
#include <string>

#include "../Debug/Assertion.hpp"
#include "../Debug/Tracer.hpp"

#include "Allocator.hpp"

#include "Hash.hpp"
#include "Exception.hpp"

namespace Lib {

/**
 * Class Map implements generic maps with keys of a class Key
 * and values of a class Value If you implement a map with
 * a new class Key. Hash is the class containing a function
 * hash() mapping keys to unsigned integer values.
 *
 * @param Key a pointer or integral value (e.g., integer or long): 
 *        anything that can be hashed to an unsigned integer
 *        and compared using ==
 * @param Val values, can be anything
 * @param Hash class containig the hash function for keys
 */
template <typename Key, typename Val,class Hash>
class Map 
{
protected:
  class Entry
  {
  public:
    /** Create a new entry */
    inline Entry ()
      : code(0)
    {
    } // Map::Entry::Entry

    /** True if the cell is occupied */
    inline bool occupied () const
    {
      return code;
    } // Map::Entry::occupied

    /** declared but not defined, to prevent on-heap allocation */
    void* operator new (size_t);

    /** Hash code, 0 if not occupied */
    unsigned code;
    /** The key of this cell (if any) */
    Key key;
    /** The value in this cell (if any) */
    Val value;
  }; // class Map::Entry

 public:
  /** Create a new map */
  Map ()
    : _capacity(0),
      _noOfEntries(0),
      _entries(0)
  {
    expand();
  } // Map::Map

  /** Deallocate the map */
  inline ~Map ()
  {
    CALL("Map::~Map");
    if (_entries) {
      DEALLOC_KNOWN(_entries,sizeof(Entry)*_capacity,"Map<>");
    }
  } // Map::~Map

  /** 
   * True if there is a value stored under this key.
   * @since 08/08/2008 Manchester
   */
  inline bool find(Key key) const
  {
    Val val;
    return find(key,val);
  }

  /** 
   * Find value by the key. The result is true if a pair with this key
   * is in the map. If such a pair is found then its value is
   * returned in found.
   *
   * @since 29/09/2002 Manchester
   */
  bool find(Key key, Val& found) const
  {
    CALL("Map::find/2");

    unsigned code = Hash::hash(key);
    if (code == 0) {
      code = 1;
    }
    Entry* entry;
    for (entry = firstEntryForCode(code);
	 entry->occupied();
	 entry = nextEntry(entry)) {
      if (entry->code == code &&
	  entry->key == key) {
	found = entry->value;
	return true;
      }
    }

    return false;
  } // Map::find
 
  /**
   * Return the first entry for @b code.
   * @since 09/12/2006 Manchester
   */
  inline Entry* firstEntryForCode(unsigned code) const
  {
    return _entries + (code % _capacity);
  } // Map::firstEntryForCode

  /**
   * Return the entry next to @b entry.
   * @since 09/12/2006 Manchester
   */
  inline Entry* nextEntry(Entry* entry) const
  {
    entry ++;
    // check if the entry is a valid one
    return entry == _afterLast ? _entries : entry;
  } // nextEntry

  /**
   * If no value under key @b key is not contained in the set, insert
   * pair (key,value) in the map.
   * Return the value stored under @b key.
   * @since 29/09/2002 Manchester
   * @since 09/12/2006 Manchester, reimplemented
   */
  inline Val insert(const Key key,Val val)
  {
    CALL("Map::insert");
    
    if (_noOfEntries >= _maxEntries) { // too many entries
      expand();
    }
    unsigned code = Hash::hash(key);
    if (code == 0) {
      code = 1;
    }
    return insert(key,val,code);
  } // Map::insert

  /**
   * Insert a pair (key,value) with a given code in the set.
   * The set must have a sufficient capacity
   * @since 09/12/2006 Manchester, reimplemented
   */
  Val insert(const Key key, Val val,unsigned code)
  {
    CALL("Map::insert/2");
    
    Entry* entry;
    for (entry = firstEntryForCode(code);
	 entry->occupied();
	 entry = nextEntry(entry)) {
      if (entry->code == code &&
	  entry->key == key) {
	return entry->value;
      }
    }
    // entry is not occupied
    _noOfEntries++;
    entry->key = key;
    entry->value = val;
    entry->code = code;
    return entry->value;
  } // Map::insert

  /**
   * If no value under key @b key is not contained in the set, insert
   * pair (key,value) in the map. Otherwise replace the value stored
   * under @b key by @b val.
   * @since 29/09/2002 Manchester
   * @since 09/12/2006 Manchester, reimplemented
   */
  void replaceOrInsert(Key key,Val val)
  {
    CALL("Map::insertOrReplace");
    
    if (_noOfEntries >= _maxEntries) { // too many entries
      expand();
    }
    unsigned code = Hash::hash(key);
    if (code == 0) {
      code = 1;
    }
    Entry* entry;
    for (entry = firstEntryForCode(code);
	 entry->occupied();
	 entry = nextEntry(entry)) {
      if (entry->code == code &&
	  entry->key == key) {
	entry->value = val;
	return;
      }
    }
    // entry is not occupied
    _noOfEntries++;
    entry->key = key;
    entry->value = val;
    entry->code = code;
  } // Map::replaceOrInsert


  /**
   * Replace the value stored under @b key by @b val.
   * (There must be a value stored under @b key).
   * @since 29/09/2002 Manchester
   * @since 09/12/2006 Manchester, reimplemented
   */
  void replace(const Key key,const Val val)
  {
    CALL("Map::replace");
    
    if (_noOfEntries >= _maxEntries) { // too many entries
      expand();
    }
    unsigned code = Hash::hash(key);
    if (code == 0) {
      code = 1;
    }
    Entry* entry;
    for (entry = firstEntryForCode(code);
	 entry->occupied();
	 entry = nextEntry(entry)) {
      if (entry->code == code &&
	  entry->key == key) {
	entry->value = val;
	return;
      }
    }
#if VDEBUG
    ASSERTION_VIOLATION;
#endif
  } // Map::replace


//   /**
//    * Replaces a value stored under the key by a new value.
//    * @warning a value under this key must already be in the table!
//    * @since 08/12/2003 Manchester
//    */
//   inline void replace (Key key, Val value)
//   {
//     // CALL("Map::replace");

//     Entry* entry = findEntry(key);

//     ASS (entry->isOccupied());

//     entry->setValue(value);
//   } // Map::replace

  /**
   * Delete all entries.
   * @since 07/08/2005 Redmond
   * @warning Works only for maps where the value type is a pointer.
   */
  void deleteAll()
  {
    CALL("Map::deleteAll");

    for (int i = _capacity-1;i >= 0;i--) {
      Entry& e = _entries[i];
      if (e.occupied()) {
	delete e.value;
      }
    }
  } // deleteAll

  /** Return the number of elements */
  inline int numberOfElements()
  {
    return _noOfEntries;
  }

  /**
   * Destroy all entries by applying destroy() to them.
   * @since 03/12/2006 Haifa
   * @warning Works only for maps where the value type is a pointer
   *          and having method destroy()
   */
  void destroyAll()
  {
    CALL("Map::destroyAll");

    for (int i = _capacity-1;i >= 0;i--) {
      Entry& e = _entries[i];
      if (e.occupied()) {
	e.value->destroy();
      }
    }
  } // destroyAll

  /** the current capacity */
  int _capacity;
  /** the current number of entries */
  int _noOfEntries;
  /** the array of entries */
  Entry* _entries;
  /** the entry after the last one, required since the 
   *  array of entries is logically a ring */
  Entry* _afterLast; // entry after the last one
  /** the maximal number of entries for this capacity */
  int _maxEntries;

  void expand()
  {
    CALL("Map::expand");

    size_t oldCapacity = _capacity;
    _capacity = _capacity ? _capacity * 2 : 32;

    Entry* oldEntries = _entries;

    void* mem = ALLOC_KNOWN(sizeof(Entry)*_capacity,"Map<>");
    _entries = new(mem) Entry [_capacity];

    _afterLast = _entries + _capacity;
    _maxEntries = (int)(_capacity * 0.8);
    // experiments using (a) random numbers (b) consecutive numbers
    // and (1) int->int 20M allocations (2) string->int 10M allocations
    // and 30,000,000 allocations
    // 0.6 : 6.80 4.87 20.8 14.9 32.6 14
    // 0.7 : 6.58 5.61 23.1 15.2 35.2 16.6
    // 0.8 : 6.36 5.77 24.0 15.4 36.0 17.4
    // 0.9 : 7.54 6.04 25.4 15.2 37.0 18.4
    // copy old entries
    Entry* current = oldEntries;
    int remaining = _noOfEntries;
    _noOfEntries = 0;
    while (remaining != 0) {
      // find first occupied entry
      while (! current->occupied()) {
	current ++;
      }
      // now current is occupied
      insert(current->key,current->value,current->code);
      current ++;
      remaining --;
    }
    if (oldEntries) {
      DEALLOC_KNOWN(oldEntries,sizeof(Entry)*oldCapacity,"Map<>");
    }
  } // Map::expand

//   /**ul
//    * Compute hash value of a key and return the entry.
//    * for this key. If there is an entry stored under this key, 
//    * then the entry is returned. Otherwise, return the non-occupied
//    * entry in which a pair (key,value) for this key can be stored.
//    * @since 29/09/2002 Manchester
//    */
//   Entry* findEntry (Key key) const
//   {
//     Entry* entry = _entries + (Hash::hash(key) % _capacity);
//     while ( entry->isOccupied() ) {
//       if (entry->key == key) {
// 	return entry;
//       }

//       entry ++;
//       // check if the entry is a valid one
//       if (entry ==_afterLast) {
// 	entry =_entries;
//       }
//     }

//     // found non-occupied entry
//     return entry;
//   } // Map::findEntry


public:
  /**
   * Class to allow iteration over keys and values stored in the map.
   * @since 13/08/2005 Novotel, Moscow
   */
  class Iterator {
  public:
    /** Create a new iterator */
    inline Iterator(const Map& map)
      : _next(map._entries),
	_last(map._afterLast)
    {
    } // Map::Iterator

    /**
     * True if there exists next element 
     * @since 13/08/2005 Novotel, Moscow
     */
    bool hasNext()
    {
      while (_next != _last) {
	if (_next->occupied()) {
	  return true;
	}
	_next++;
      }
      return false;
    }

    /**
     * Return the next value
     * @since 13/08/2005 Novotel, Moscow
     * @warning hasNext() must have been called before
     */
    Val next()
    {
      ASS(_next != _last);
      ASS(_next->occupied());
      Val result = _next->value;
      _next++;
      return result;
    }
  private:
    /** iterator will look for the next occupied cell starting with this one */
    Entry* _next;
    /** iterator will stop looking for the next cell after reaching this one */
    Entry* _last;
  };
}; // class Map

} // namespace Lib

#endif // __Map__

