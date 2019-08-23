#ifndef _qe_h_
#define _qe_h_

#include <vector>
#include <limits>
#include <map>

#include "../rbf/rbfm.h"
#include "../rm/rm.h"
#include "../ix/ix.h"

#define QE_EOF (-1)  // end of the index scan

using namespace std;

typedef enum{ MIN=0, MAX, COUNT, SUM, AVG } AggregateOp;

// The following functions use the following
// format for the passed data.
//    For INT and REAL: use 4 bytes
//    For VARCHAR: use 4 bytes for the length followed by the characters

struct Value {
    AttrType type;          // type of value
    void     *data;         // value

	bool operator<(const Value& rhs) const;
};


struct Condition {
    string  lhsAttr;        // left-hand side attribute
    CompOp  op;             // comparison operator
    bool    bRhsIsAttr;     // TRUE if right-hand side is an attribute and not a value; FALSE, otherwise.
    string  rhsAttr;        // right-hand side attribute if bRhsIsAttr = TRUE
    Value   rhsValue;       // right-hand side value if bRhsIsAttr = FALSE
};


class Iterator {
    // All the relational operators and access methods are iterators.
    public:
        virtual RC getNextTuple(void *data) = 0;
        virtual void getAttributes(vector<Attribute> &attrs) const = 0;
        virtual ~Iterator() {};
};


class TableScan : public Iterator
{
    // A wrapper inheriting Iterator over RM_ScanIterator
    public:
        RelationManager &rm;
        RM_ScanIterator *iter;
        string tableName;
        vector<Attribute> attrs;
        vector<string> attrNames;
        RID rid;

        TableScan(RelationManager &rm, const string &tableName, const char *alias = NULL):rm(rm)
        {
        	//Set members
        	this->tableName = tableName;

            // Get Attributes from RM
            rm.getAttributes(tableName, attrs);

            // Get Attribute Names from RM
            unsigned i;
            for(i = 0; i < attrs.size(); ++i)
            {
                // convert to char *
                attrNames.push_back(attrs.at(i).name);
            }

            // Call RM scan to get an iterator
            iter = new RM_ScanIterator();
            rm.scan(tableName, "", NO_OP, NULL, attrNames, *iter);

            // Set alias
            if(alias) this->tableName = alias;
        };

        // Start a new iterator given the new compOp and value
        void setIterator()
        {
            iter->close();
            delete iter;
            iter = new RM_ScanIterator();
            rm.scan(tableName, "", NO_OP, NULL, attrNames, *iter);
        };

        RC getNextTuple(void *data)
        {
            return iter->getNextTuple(rid, data);
        };

        void getAttributes(vector<Attribute> &attrs) const
        {
            attrs.clear();
            attrs = this->attrs;
            unsigned i;

            // For attribute in vector<Attribute>, name it as rel.attr
            for(i = 0; i < attrs.size(); ++i)
            {
                string tmp = tableName;
                tmp += ".";
                tmp += attrs.at(i).name;
                attrs.at(i).name = tmp;
            }
        };

        ~TableScan()
        {
        	iter->close();
        };
};


class IndexScan : public Iterator
{
    // A wrapper inheriting Iterator over IX_IndexScan
    public:
        RelationManager &rm;
        RM_IndexScanIterator *iter;
        string tableName;
        string attrName;
        vector<Attribute> attrs;
        char key[PAGE_SIZE];
        RID rid;

        IndexScan(RelationManager &rm, const string &tableName, const string &attrName, const char *alias = NULL):rm(rm)
        {
        	// Set members
        	this->tableName = tableName;
        	this->attrName = attrName;


            // Get Attributes from RM
            rm.getAttributes(tableName, attrs);

            // Call rm indexScan to get iterator
            iter = new RM_IndexScanIterator();
            rm.indexScan(tableName, attrName, NULL, NULL, true, true, *iter);

            // Set alias
            if(alias) this->tableName = alias;
        };

        // Start a new iterator given the new key range
        void setIterator(void* lowKey,
                         void* highKey,
                         bool lowKeyInclusive,
                         bool highKeyInclusive)
        {
            iter->close();
            delete iter;
            iter = new RM_IndexScanIterator();
            rm.indexScan(tableName, attrName, lowKey, highKey, lowKeyInclusive,
                           highKeyInclusive, *iter);
        };

        RC getNextTuple(void *data)
        {
            int rc = iter->getNextEntry(rid, key);
            if(rc == 0)
            {
                rc = rm.readTuple(tableName.c_str(), rid, data);
            }
            return rc;
        };

        void getAttributes(vector<Attribute> &attrs) const
        {
            attrs.clear();
            attrs = this->attrs;
            unsigned i;

            // For attribute in vector<Attribute>, name it as rel.attr
            for(i = 0; i < attrs.size(); ++i)
            {
                string tmp = tableName;
                tmp += ".";
                tmp += attrs.at(i).name;
                attrs.at(i).name = tmp;
            }
        };

        ~IndexScan()
        {
            iter->close();
        };
};


class Filter : public Iterator {
    // Filter operator
    public:
        Filter(Iterator *input,               // Iterator of input R
               const Condition &condition     // Selection condition
        );
        ~Filter(){};

        RC getNextTuple(void *data);
        // For attribute in vector<Attribute>, name it as rel.attr
        void getAttributes(vector<Attribute> &attrs) const;

		Iterator* input;
		Condition condition;
		int leftIndex;
		int rightIndex;
		vector<Attribute> attrs;
		bool end;
};


class Project : public Iterator {
    // Projection operator
    public:
        Project(Iterator *input,                    // Iterator of input R
              const vector<string> &attrNames);   // vector containing attribute names
        ~Project(){};

        RC getNextTuple(void *data);
        // For attribute in vector<Attribute>, name it as rel.attr
        void getAttributes(vector<Attribute> &attrs) const;

		Iterator* input;
		vector<Attribute> attrs;
		vector<int> attrIndexes;
		size_t totalAttrsCount;
		bool end;
		char buffer[PAGE_SIZE];
};

class Tuple{
public:
    void *data;
    int length;
    Tuple(void *data,int length);
};

class BNLJoin : public Iterator {
    // Block nested-loop join operator
    public:
        BNLJoin(Iterator *leftIn,            // Iterator of input R
               TableScan *rightIn,           // TableScan Iterator of input S
               const Condition &condition,   // Join condition
               const unsigned numPages       // # of pages that can be loaded into memory,
			                                 //   i.e., memory block size (decided by the optimizer)
        );
        ~BNLJoin(){};
        int sum_buffer=0;
        vector<Tuple> outers;
        int outer_index = 0;
        vector<Tuple> inners;
        int inner_index = 0;
        Iterator *leftIn;
        TableScan *rightIn;
        Condition condition;
        unsigned numPages;
        vector<Attribute> attrs_out;
        vector<Attribute> attrs_in;
        RC getNextTuple(void *data);
        // For attribute in vector<Attribute>, name it as rel.attr
        void getAttributes(vector<Attribute> &attrs) const;
        RC getByteLength(vector<Attribute> attrs,void *data,int &size);
        RC updateVectorOuter();
        RC updateVectorInner();
        RC find_r_value(int &attrtype,int &value_int,float &value_float,string &value_string,int attr_index);
        RC find_s_value(int &attrtype,int &value_int,float &value_float,string &value_string,int inner_index,int attr_index);
        RC getJoin(void *data,void *r_data,int outer_length,void *s_data,int inner_length);

};

class INLJoin : public Iterator {
    // Index nested-loop join operator
    public:
        INLJoin(Iterator *leftIn,           // Iterator of input R
               IndexScan *rightIn,          // IndexScan Iterator of input S
               const Condition &condition   // Join condition
        );
		~INLJoin() {};

        RC getNextTuple(void *data);
        // For attribute in vector<Attribute>, name it as rel.attr
        void getAttributes(vector<Attribute> &attrs) const;
		RC readFromRight(IndexScan* rightScan, void * data);
		RC outputJoinResult(void *data);

		Iterator* leftIn;
		IndexScan* rightIn;
		Condition condition;

		int leftIndex;
		int rightIndex;
		vector<Attribute> leftAttrs;
		vector<Attribute> rightAttrs;
		char leftBuffer[PAGE_SIZE];
		char rightBuffer[PAGE_SIZE];

		bool end;
};

// Optional for everyone. 10 extra-credit points
class GHJoin : public Iterator {
    // Grace hash join operator
    public:
      GHJoin(Iterator *leftIn,               // Iterator of input R
            Iterator *rightIn,               // Iterator of input S
            const Condition &condition,      // Join condition (CompOp is always EQ)
            const unsigned numPartitions     // # of partitions for each relation (decided by the optimizer)
      );
      ~GHJoin(){};
      Iterator *leftIn;
      Iterator *rightIn;
      Condition condition;
      unsigned numPartitions;
      vector<Attribute> attrs_out;
      vector<Attribute> attrs_in;
      RM_ScanIterator rm_ite;
      int vector_index=0;
      int s_index=0;
      int cur_partition=0;
      map<int,vector<Tuple>> map1;
      map<int,vector<Tuple>>::iterator it1;
      map<float,vector<Tuple>> map2;
      map<float,vector<Tuple>>::iterator it2;
      map<string,vector<Tuple>> map3;
      map<string,vector<Tuple>>::iterator it3;
      vector<string> string_vector_in;
      vector<string> string_vector_out;
      string r_tableName;
      string s_tableName;
      int name_id;
      static int uniq_id;
      RID rid;
      void* data_s;
      int type=0;
      RC getNextTuple(void *data);
      RC find_r_value(int &attrtype, int &value_int, float &value_float, string &value_string, int attr_index,void* data);
      RC find_s_value(int &attrtype, int &value_int, float &value_float, string &value_string, int attr_index,void *data);
      RC map_rPartitions(int r_index);
      RC getByteLength(vector<Attribute> attrs, void *data, int &size);
      RC getJoin(void *data, void *r_data, int outer_length, void *s_data, int inner_length);
      void fillLeftPartitions();
      void fillRightPartitions();
      // For attribute in vector<Attribute>, name it as rel.attr
      void getAttributes(vector<Attribute> &attrs) const;
};

struct AggregateResult
{
	float avg;
	float count;
	float max;
	float min;
	float sum;

	AggregateResult()
	{
		this->avg = 0;
		this->count = 0;
		this->max = numeric_limits<float>::min();
		this->min = numeric_limits<float>::max();
		this->sum = 0;
	}
};

class Aggregate : public Iterator {
    // Aggregation operator
    public:
        // Mandatory
        // Basic aggregation
        Aggregate(Iterator *input,          // Iterator of input R
                  Attribute aggAttr,        // The attribute over which we are computing an aggregate
                  AggregateOp op            // Aggregate operation
        );

        // Optional for everyone: 5 extra-credit points
        // Group-based hash aggregation
        Aggregate(Iterator *input,             // Iterator of input R
                  Attribute aggAttr,           // The attribute over which we are computing an aggregate
                  Attribute groupAttr,         // The attribute over which we are grouping the tuples
                  AggregateOp op              // Aggregate operation
        );
        ~Aggregate();

        RC getNextTuple(void *data);
        // Please name the output attribute as aggregateOp(aggAttr)
        // E.g. Relation=rel, attribute=attr, aggregateOp=MAX
        // output attrname = "MAX(rel.attr)"
        void getAttributes(vector<Attribute> &attrs) const;

		Iterator *input;                              // Iterator of input R
		Attribute aggAttr;                            // The attribute over which we are computing an aggregate
		AggregateOp op;                               // Aggregate operation
		vector<Attribute> attrs;
		int attrIndex;
		bool end;
		map<Value, AggregateResult> groupResult;
		map<Value, AggregateResult>::iterator groupResultIter;
		bool isGroupby;
};

#endif