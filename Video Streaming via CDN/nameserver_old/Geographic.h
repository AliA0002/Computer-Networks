#ifndef __GEOGRAPHIC_H__
#define __GEOGRAPHIC_H__

#include <list>
#include <queue>
#include <vector>

using namespace std;

class Graph {
    int V;                          // Num of vertices
    list<pair<int, int>> *adj_list; // List of vertices and their destinations

  public:
    Graph(int V) {
        this->V = V;
        adj_list = new list<pair<int, int>>[V];
    }
    void buildEdges(vector<vector<int>> l); // Builds the edges from the links
    int shortestPath(int source_node,
                     vector<int> other_nodes); // Finds the shortest path from the source node to the other nodes
};

void Graph::buildEdges(vector<vector<int>> l) {
    for (size_t i = 0; i < l.size(); ++i) {
        adj_list[l[i][0]].push_back(make_pair(l[i][1], l[i][2]));
        adj_list[l[i][1]].push_back(make_pair(l[i][0], l[i][2]));
    }
}

int Graph::shortestPath(int source_node, vector<int> other_nodes) {
    priority_queue<pair<int, int>, vector<pair<int, int>>, greater<pair<int, int>>>
        pq; // Priority queue to store vertices
    vector<int> cost(V, 1000000);
    pq.push(make_pair(0, source_node));
    cost[source_node] = 0;
    priority_queue<pair<int, int>, vector<pair<int, int>>, greater<pair<int, int>>> shortest_path;

    while (!pq.empty()) {
        int x = pq.top().second;
        pq.top();
        list<pair<int, int>>::iterator it;
        for (it = adj_list[x].begin(); it != adj_list[x].end(); ++it) {
            int node = (*it).first;
            int link_cost = (*it).second;
            if (cost[node] > cost[x] + link_cost) {
                cost[node] = cost[x] + link_cost;
                pq.push(make_pair(cost[node], node));
            }
        }
    }

    printf("STUFF");

    for (size_t i = 0; i < other_nodes.size(); ++i) {
        int destination = other_nodes[i];
        shortest_path.push(make_pair(cost[destination], destination));
    }
    return shortest_path.top().second; // Returns closest node;
}

#endif