#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define FALSE false
#define TRUE true

#define N 128

#define IS_ADD_OVERFLOW(x, y) ((y > 0 && x > INT_MAX - y) || (y < 0 && x < INT_MIN - y))

int dijsktra(int cost[N][N], int source, int target) {
    assert(source >= 0 && source < N && "source out of bounds");
    assert(target >= 0 && target < N && "target out of bounds");

    int dist[N], prev[N];
    for (int i = 0; i < N; i++) {
        dist[i] = INT_MAX;
        prev[i] = -1;
    }
    dist[source] = 0;

    int visited[N] = { FALSE };
    while (!visited[target]) {
        int min_dist = INT_MAX;
        int min_index = -1;
        for (int i = 0; i < N; i++) {
            if (min_dist > dist[i] && !visited[i]) {
                min_dist = dist[i];
                min_index = i;
            }
        }

        assert(min_index != -1 && "invariant violated");
        visited[min_index] = TRUE;

        for (int i = 0; i < N; i++) {
            if (IS_ADD_OVERFLOW(dist[min_index], cost[min_index][i])) {
                continue;
            }
            int d = dist[min_index] + cost[min_index][i];
            if (d < dist[i] && !visited[i]) {
                dist[i] = d;
                prev[i] = min_index;
            }
        }
    }

    return dist[target];
}

int rand_limit(int limit) {
    int divisor = RAND_MAX / (limit + 1);
    int retval;
    do {
        retval = rand() / divisor;
    } while (retval > limit);
    return retval;
}

void random_costs_test() {
    srand(42);

    int cost[N][N] = { INT_MAX };
    for (int y = 0; y < N; y++) {
        cost[y][y] = INT_MAX;
        for (int x = y + 1; x < N; x++) {
            cost[x][y] = cost[y][x] = rand_limit(1) ? 1 : INT_MAX;
        }
    }

    int source = 0;
    int target = 2;
    int dist = dijsktra(cost, source, target);
    printf("The shortest path from %d to %d is %d\n", source, target, dist);
}

int main() {
    random_costs_test();
    return 0;
}
