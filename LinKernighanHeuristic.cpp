//
// Created by Karl Welzel on 14.05.19.
//

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>
#include "Tour.h"
#include "TsplibUtils.h"
#include "LinKernighanHeuristic.h"
#include "AlphaDistances.h"

// ============================================= AlternatingWalk class =================================================

AlternatingWalk AlternatingWalk::close() const {
    AlternatingWalk result(*this);
    result.push_back(at(0));
    return result;
}

AlternatingWalk AlternatingWalk::appendAndClose(vertex_t vertex) const {
    AlternatingWalk result(*this);
    result.push_back(vertex);
    result.push_back(at(0));
    return result;
}

bool AlternatingWalk::containsEdge(vertex_t vertex1, vertex_t vertex2) const {
    for (dimension_t i = 0; i < size() - 1; ++i) {
        if ((operator[](i) == vertex1 and operator[](i + 1) == vertex2) or
            (operator[](i) == vertex2 and operator[](i + 1) == vertex1)) {
            return true;
        }
    }
    return false;
}

std::ostream &operator<<(std::ostream &out, const AlternatingWalk &walk) {
    std::string output;
    for (vertex_t vertex : walk) {
        output += std::to_string(vertex) + ", ";
    }
    out << output.substr(0, output.length() - 2);
    return out;
}


// ============================================= CandidateEdges class =================================================

CandidateEdges::CandidateEdges(dimension_t dimension, const std::vector<vertex_t> &fillValue) : edges(dimension,
                                                                                                      fillValue) {}

std::vector<vertex_t> &CandidateEdges::operator[](size_t index) {
    return edges[index];
}

CandidateEdges CandidateEdges::allNeighbors(const TsplibProblem &problem) {
    std::vector<vertex_t> allVertices(problem.getDimension());
    std::iota(allVertices.begin(), allVertices.end(), 0);

    CandidateEdges result(problem.getDimension(), allVertices);
    for (vertex_t v : allVertices) {
        // Delete v from its neighbors
        result[v].erase(std::remove(result[v].begin(), result[v].end(), v), result[v].end());
    }
    return result;
}

CandidateEdges CandidateEdges::rawNearestNeighbors(
        dimension_t dimension, size_t k, const std::function<bool(vertex_t, vertex_t, vertex_t)> &distCompare) {
    std::unordered_set<vertex_t> allVertices{};
    for (vertex_t v = 0; v < dimension; ++v) {
        allVertices.insert(v);
    }

    CandidateEdges result(dimension, std::vector<vertex_t>(k));
    for (vertex_t v = 0; v < dimension; ++v) {
        // Sort the k nearest neighbors of v by distance to v and put them in result[v]
        allVertices.erase(v); // Don't include v in the search
        std::partial_sort_copy(allVertices.begin(), allVertices.end(), result[v].begin(), result[v].end(),
                               std::bind(distCompare, v, std::placeholders::_1, std::placeholders::_2));
        allVertices.insert(v);
    }
    return result;
}

CandidateEdges CandidateEdges::nearestNeighbors(const TsplibProblem &problem, size_t k) {
    auto distCompare = [&problem](vertex_t v, vertex_t w1, vertex_t w2) {
        return problem.dist(v, w1) < problem.dist(v, w2);
    };

    return rawNearestNeighbors(problem.getDimension(), k, distCompare);
}

CandidateEdges CandidateEdges::alphaNearestNeighbors(const TsplibProblem &problem, size_t k) {
    auto dist = [&problem](vertex_t i, vertex_t j) { return problem.dist(i, j); };

    // Compute the alpha distances
    std::vector<std::vector<distance_t>> alpha = alphaDistances(problem.getDimension(), dist);

    auto distCompare = [&problem, &alpha](vertex_t v, vertex_t w1, vertex_t w2) {
        return std::make_tuple(alpha[v][w1], problem.dist(v, w1)) <
               std::make_tuple(alpha[v][w2], problem.dist(v, w2));
    };
    return rawNearestNeighbors(problem.getDimension(), k, distCompare);
}

CandidateEdges CandidateEdges::optimizedAlphaNearestNeighbors(const TsplibProblem &problem, size_t k) {
    auto dist = [&problem](vertex_t i, vertex_t j) { return problem.dist(i, j); };

    // Compute the optimized alpha distances
    std::vector<std::vector<distance_t>> alpha = optimizedAlphaDistances(problem.getDimension(), dist);

    auto distCompare = [&problem, &alpha](vertex_t v, vertex_t w1, vertex_t w2) {
        return std::make_tuple(alpha[v][w1], problem.dist(v, w1)) <
               std::make_tuple(alpha[v][w2], problem.dist(v, w2));
    };
    return rawNearestNeighbors(problem.getDimension(), k, distCompare);
}

CandidateEdges CandidateEdges::create(const TsplibProblem &problem, CandidateEdges::Type candidateEdgeType,
                                      size_t k) {
    switch (candidateEdgeType) {
        case Type::ALL_NEIGHBORS:
            return CandidateEdges::allNeighbors(problem);
        case Type::NEAREST_NEIGHBORS:
            return CandidateEdges::nearestNeighbors(problem, k);
        case Type::ALPHA_NEAREST_NEIGHBORS:
            return CandidateEdges::alphaNearestNeighbors(problem, k);
        default:
        case Type::OPTIMIZED_ALPHA_NEAREST_NEIGHBORS:
            return CandidateEdges::optimizedAlphaNearestNeighbors(problem, k);
    }
}

// ============================================= linKernighanHeuristic =================================================

LinKernighanHeuristic::LinKernighanHeuristic(TsplibProblem &tsplibProblem, CandidateEdges candidateEdges)
        : tsplibProblem(tsplibProblem), candidateEdges(std::move(candidateEdges)) {
}

vertex_t LinKernighanHeuristic::chooseRandomElement(const std::vector<vertex_t> &elements) {
    std::random_device randomNumberGenerator;
    std::uniform_int_distribution<size_t> distribution(0, elements.size() - 1);
    return elements[distribution(randomNumberGenerator)];
}

Tour LinKernighanHeuristic::generateRandomTour() {
    // Initialize a array with all vertices as the remaining vertices (that are to be placed on the tour)
    std::vector<vertex_t> remainingVertices(tsplibProblem.getDimension());
    std::iota(remainingVertices.begin(), remainingVertices.end(), 0);

    // This variable store the order of the vertices on the tour
    std::vector<vertex_t> tourOrder;

    // Start with a random vertex
    vertex_t currentVertex = chooseRandomElement(remainingVertices);
    remainingVertices.erase(remainingVertices.begin() + currentVertex); // The index of currentVertex is currentVertex
    tourOrder.push_back(currentVertex);


    // In each step, decide for each vertex otherVertex if it is an element in one or more of these categories
    // (1) otherVertex was not already chosen and {currentVertex, otherVertex} is a candidate edge and on the
    //     current best tour
    // (2) otherVertex was not already chosen and {currentVertex, otherVertex} is a candidate edge
    // (3) otherVertex was not already chosen
    // Choose the next current vertex randomly from the first non-empty category above

    std::vector<vertex_t> candidatesInBestTour; // Category (1)
    std::vector<vertex_t> candidates; // Category (2)
    // Category (3) is remainingVertices
    while (!remainingVertices.empty()) {
        candidatesInBestTour.clear();
        candidates.clear();
        for (vertex_t otherVertex : candidateEdges[currentVertex]) {
            if (std::find(remainingVertices.begin(), remainingVertices.end(), otherVertex) != remainingVertices.end()) {
                // otherVertex is an element of remainingVertices
                if (currentBestTour.getDimension() != 0 and currentBestTour.containsEdge(currentVertex, otherVertex)) {
                    candidatesInBestTour.push_back(otherVertex);
                }
                candidates.push_back(otherVertex);
            }
        }

        if (!candidatesInBestTour.empty()) {
            currentVertex = chooseRandomElement(candidatesInBestTour);
        } else if (!candidates.empty()) {
            currentVertex = chooseRandomElement(candidates);
        } else {
            currentVertex = chooseRandomElement(remainingVertices);
        }
        remainingVertices.erase(std::remove(remainingVertices.begin(), remainingVertices.end(), currentVertex),
                                remainingVertices.end());
        tourOrder.push_back(currentVertex);
    }

    return Tour(tourOrder);
}

Tour LinKernighanHeuristic::improveTour(const Tour &startTour) {
    const dimension_t dimension = tsplibProblem.getDimension();

    Tour currentTour = startTour;
    std::vector<std::vector<vertex_t>> vertexChoices;
    AlternatingWalk currentWalk;
    AlternatingWalk bestAlternatingWalk;
    signed_distance_t highestGain = 0;

    while (true) {
        // Create set X_0 with all vertices
        vertexChoices.clear();
        vertexChoices.emplace_back(dimension);
        std::iota(vertexChoices[0].begin(), vertexChoices[0].end(), 0);

        currentWalk.clear();
        bestAlternatingWalk.clear();
        highestGain = 0;
        size_t i = 0;
        while (true) {
            if (vertexChoices[i].empty()) {
                if (highestGain > 0) {
                    currentTour.exchange(bestAlternatingWalk);
                    break;
                } else { // highestGain == 0
                    if (i == 0) {
                        return currentTour;
                    } else {
                        i = std::min(i - 1, backtrackingDepth);
                        vertexChoices.erase(vertexChoices.begin() + i + 1, vertexChoices.end());
                        currentWalk.erase(currentWalk.begin() + i, currentWalk.end());
                        continue;
                    }
                }
            }

            currentWalk.push_back(vertexChoices[i].back());
            vertexChoices[i].pop_back();

            if (i % 2 == 1 and i >= 3) {
                AlternatingWalk closedWalk = currentWalk.close(); // closedWalk = (x_0, x_1, ..., x_i, x_0)
                signed_distance_t gain = tsplibProblem.exchangeGain(closedWalk);
                if (gain > highestGain and currentTour.isTourAfterExchange(closedWalk)) {
                    //std::cout << "New highest gain: " << gain << ", value before: " << highestGain << std::endl;
                    bestAlternatingWalk = closedWalk;
                    highestGain = gain;
                }
            }

            vertexChoices.emplace_back(); // Add set X_{i+1}
            vertex_t xi = currentWalk[i];
            if (i % 2 == 1) { // i is odd
                // Determine possible in-edges
                signed_distance_t currentGain = tsplibProblem.exchangeGain(currentWalk);
                vertex_t xiPredecessor = currentTour.predecessor(xi);
                vertex_t xiSuccessor = currentTour.successor(xi);
                for (vertex_t x : candidateEdges[xi]) {
                    if (x != currentWalk[0]
                        and x != xiPredecessor and x != xiSuccessor // equivalent to !currentTour.containsEdge(xi, x)
                        and !currentWalk.containsEdge(xi, x)
                        and currentGain - static_cast<signed_distance_t>(tsplibProblem.dist(xi, x)) > highestGain) {

                        vertexChoices[i + 1].push_back(x);
                    }
                }
            } else { // i is even
                // Determine possible out-edges
                // No out-edge should connect back to currentWalk[0], because at this point currentWalk is not a valid
                // alternating walk (even number of elements) and can never be closed in the future
                if (i == 0 and currentBestTour.getDimension() != 0) {
                    // The first edge to be broken may not be on the currently best solution tour
                    vertex_t x0Predecessor = currentBestTour.predecessor(currentWalk[0]);
                    vertex_t x0Successor = currentBestTour.successor(currentWalk[0]);
                    for (vertex_t neighbor : currentTour.getNeighbors(xi)) {
                        if (neighbor != currentWalk[0] and neighbor != x0Predecessor and neighbor != x0Successor) {
                            vertexChoices[i + 1].push_back(neighbor);
                        }
                    }
                } else if (i <= infeasibilityDepth) {
                    for (vertex_t neighbor : currentTour.getNeighbors(xi)) {
                        if (neighbor != currentWalk[0] and !currentWalk.containsEdge(xi, neighbor)) {
                            vertexChoices[i + 1].push_back(neighbor);
                        }
                    }
                } else {
                    for (vertex_t neighbor : currentTour.getNeighbors(xi)) {
                        // currentWalk.appendAndClose(neighbor) is not a valid alternating walk if {neighbor, x_0} is an
                        // edge in currentWalk, but this is only possible if neighbor is x_1, so we only need to exclude
                        // this special case
                        if (neighbor != currentWalk[0]
                            and !currentWalk.containsEdge(xi, neighbor)
                            and neighbor != currentWalk[1]
                            and currentTour.isTourAfterExchange(currentWalk.appendAndClose(neighbor))) {
                            vertexChoices[i + 1].push_back(neighbor);
                        }
                    }
                }
            }

            ++i;
        }

    }
}

Tour LinKernighanHeuristic::findBestTour(size_t numberOfTrials, distance_t optimumTourLength, double acceptableError,
                                         bool verboseOutput) {
    if (numberOfTrials < 1) {
        throw std::runtime_error("The number of trials can not be lower than 1.");
    }

    Tour startTour;
    Tour currentTour;
    distance_t currentBestLength = std::numeric_limits<distance_t>::max();
    size_t trialCount = 0;

    while (trialCount++ < numberOfTrials) {
        if (verboseOutput) std::cout << "Trial " << trialCount << " | " << std::flush;

        startTour = generateRandomTour();
        if (verboseOutput)
            std::cout << "Length of startTour: " << tsplibProblem.length(startTour) << " | " << std::flush;

        currentTour = improveTour(startTour);
        if (verboseOutput)
            std::cout << "Length of currentTour: " << tsplibProblem.length(currentTour) << " | " << std::flush;

        if (tsplibProblem.length(currentTour) < currentBestLength) {
            currentBestTour = currentTour;
            currentBestLength = tsplibProblem.length(currentBestTour);
        }
        if (verboseOutput) std::cout << "Length of currentBestTour: " << currentBestLength << std::endl;

        // Stop if the increase in length of the current best tour relative to the optimal length is below the
        // threshold set by acceptableError
        if (tsplibProblem.length(currentBestTour) < (1 + acceptableError) * optimumTourLength) {
            break;
        }
    }

    return currentBestTour;
}
