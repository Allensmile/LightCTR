//
//  gbm_algo_abst.h
//  LightCTR
//
//  Created by SongKuangshi on 2017/9/26.
//  Copyright © 2017年 SongKuangshi. All rights reserved.
//

#ifndef gbm_algo_abst_h
#define gbm_algo_abst_h

#include <iostream>
#include <vector>
#include <list>
#include <map>
#include <fstream>
#include <string>
#include <thread>
#include <cmath>
#include "assert.h"
using namespace std;

class GBM_Algo_Abst {
protected:
    struct LeafNodeStat;
    struct RegTreeNode {
        RegTreeNode *left, *right, *father;
        double split_threshold;
        int split_feature_index;
        bool dataNAN_go_Right;

        size_t node_index;
        LeafNodeStat* leafStat;
        
        RegTreeNode(RegTreeNode *_father, bool bLeft, size_t index) {
            left = right = NULL;
            father = _father;
            node_index = index;
            if (father)
                bLeft ? father->left = this : father->right = this;
            leafStat = NULL;
            dataNAN_go_Right = 0;
            split_threshold = split_feature_index = -1;
        }
        bool operator== (const RegTreeNode& node) const {
            return node_index == node.node_index;
        }
    };
    struct LeafNodeStat {
        RegTreeNode* treeNode;
        double weight;
        size_t data_cnt;
        double gain;
        double sumGrad, sumHess;
        bool active;
        LeafNodeStat(RegTreeNode *_node) {
            active = true;
            weight = data_cnt = gain = sumGrad = sumHess = 0;
            treeNode = _node;
            treeNode->leafStat = this;
        }
        LeafNodeStat(const LeafNodeStat& _node) {
            active = true;
            weight = _node.weight, data_cnt = _node.data_cnt;
            gain = _node.gain, sumGrad = _node.sumGrad, sumHess = _node.sumHess;
            treeNode = _node.treeNode;
            treeNode->leafStat = this;
        }
        bool operator== (const LeafNodeStat& t) const {
            return treeNode->node_index == t.treeNode->node_index;
        }
        inline bool needUpdate(double splitGain, size_t split_index) {
            assert(!isnan(splitGain));
            assert(split_index >= 0);
            if (treeNode->split_feature_index <= split_index) {
                return splitGain > this->gain;
            } else {
                return !(this->gain > splitGain);
            }
        }
    };
public:
    GBM_Algo_Abst(string _dataPath, size_t _maxDepth, size_t _minLeafW) :
    maxDepth(_maxDepth), minLeafW(_minLeafW) {
        feature_cnt = 0;
        node_cnt = 0;
        RegTreeRootArr.clear();
        loadDataRow(_dataPath);
    }
    virtual ~GBM_Algo_Abst() {
        delete [] dataSet_Pred;
    }
    
    inline RegTreeNode* newTree() {
        node_cnt = 0;
        RegTreeNode* root = newNode(NULL, 1);
        RegTreeRootArr.push_back(root);
        leafNodes_tmp.clear();
        leafNodes_tmp.push_back(new LeafNodeStat(root));
        return root;
    }
    inline RegTreeNode* newNode(RegTreeNode *root, bool bLeft) {
        RegTreeNode *node = new RegTreeNode(root, bLeft, node_cnt++);
        return node;
    }
    
    inline void turn_leaf(RegTreeNode *node) {
        node->leafStat = new LeafNodeStat(*node->leafStat); // deep copy from LeafNodes vector to save leaf info
        node->leafStat->active = false;
    }
    
    inline pair<RegTreeNode*, RegTreeNode*> split_node(RegTreeNode *node) {
        RegTreeNode *leftNode = newNode(node, 1);
        RegTreeNode *rightNode = newNode(node, 0);
        {
            auto it = find(leafNodes.begin(), leafNodes.end(), node->leafStat);
            assert(it != leafNodes.end());
        }
        node->leafStat = NULL;
        
        leafNodes_tmp.push_back(new LeafNodeStat(leftNode));
        leafNodes_tmp.push_back(new LeafNodeStat(rightNode));
        return make_pair(leftNode, rightNode);
    }
    
    inline RegTreeNode* nextLevel(RegTreeNode* root, map<size_t, double>& dataRow) {
        assert(!bLeaf(root));
        
        bool go_left = 1;
        if (dataRow.find(root->split_feature_index) == dataRow.end()) {
            // this data row don't have split feature, go default
            root->dataNAN_go_Right ? go_left = 0 : go_left = 1;
        } else {
            double threshold = root->split_threshold;
            dataRow[root->split_feature_index] < threshold ? go_left = 1 : go_left = 0;
        }
        assert(go_left ? root->left : root->right);
        return go_left ? root->left : root->right;
    }
    
    inline bool bLeaf(RegTreeNode* root) {
        assert(((root->left == NULL && root->right == NULL) ^ (root->leafStat != NULL)) == 0);
        return root->left == NULL && root->right == NULL;
    }
    
    inline double locAtLeafWeight(RegTreeNode* root, map<size_t, double>& dataRow) {
        while (!bLeaf(root)) {
            root = nextLevel(root, dataRow);
        }
        return root->leafStat->weight;
    }
    
    void loadDataRow(string dataPath) {
        dataSet.clear();
        dataSet_feature.clear();
        
        ifstream fin_;
        string line;
        int nchar, y;
        size_t fid, rid = 0;
        int val;
        fin_.open(dataPath, ios::in);
        if(!fin_.is_open()){
            cout << "open file error!" << endl;
            exit(1);
        }
        map<size_t, double> tmp;
//        getline(fin_, line);
        while(!fin_.eof()){
            getline(fin_, line);
            tmp.clear();
            const char *pline = line.c_str();
            if(sscanf(pline, "%d%n", &y, &nchar) >= 1){
                pline += nchar + 1;
                y = y < 5 ? 0 : 1;
                label.push_back(y);
                fid = 0;
                while(pline < line.c_str() + (int)line.length() &&
                      sscanf(pline, "%d%n", &val, &nchar) >= 1){
                    pline += nchar + 1;
                    if (*pline == ',')
                        pline += 1;
                    fid++;
                    if (val == 0) {
                        continue;
                    }
                    tmp[fid] = val;
                    dataSet_feature[fid].push_back(make_pair(rid, val));
                }
                assert(!tmp.empty());
                this->feature_cnt = max(this->feature_cnt, fid + 1);
            }
            if (tmp.empty()) {
                continue;
            }
            this->dataSet.push_back(tmp);
            rid++;
        }
        this->dataRow_cnt = this->dataSet.size();
        assert(dataRow_cnt > 0 && label.size() == dataRow_cnt);
    }
    
    virtual void Train() = 0;
    
    size_t node_cnt;
    
    vector<RegTreeNode*> RegTreeRootArr;
    list<LeafNodeStat*> leafNodes, leafNodes_tmp;
    vector<pair<double, double> > dataSet_Grad;
    map<size_t, vector<pair<size_t, double> > > dataSet_feature;
    
    int has_pred_tree;
    double* dataSet_Pred;
    
    size_t maxDepth, minLeafW;
    size_t feature_cnt, dataRow_cnt;
    vector<map<size_t, double> > dataSet;
    vector<int> label;
};

#endif /* gbm_algo_abst_h */
