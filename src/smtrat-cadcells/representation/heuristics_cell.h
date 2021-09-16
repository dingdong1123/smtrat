namespace smtrat::cadcells::representation {

datastructures::IndexedRoot simplest_bound(datastructures::Projections& proj, const std::vector<datastructures::IndexedRoot>& bounds) { // TODO later: improve
    assert(!bounds.empty());
    // return *bounds.begin();

    auto simplest = bounds.begin();
    for (auto iter = bounds.begin(); iter != bounds.end(); iter++) {
        if (proj.degree(iter->poly) < proj.degree(simplest->poly)) {
            simplest = iter;
        }
    }

    return *simplest;
}

datastructures::CellDescription compute_simplest_cell(datastructures::Projections& proj, const datastructures::DelineationInterval& del) {
    if (del.is_section()) {
        return datastructures::CellDescription(simplest_bound(proj, del.lower()->second));
    } else if (del.lower_unbounded() && del.upper_unbounded()) {
        return datastructures::CellDescription(datastructures::Bound::infty, datastructures::Bound::infty);
    } else if (del.lower_unbounded() ) {
        return datastructures::CellDescription(datastructures::Bound::infty, simplest_bound(proj, del.upper()->second));
    } else if (del.upper_unbounded()) {
        return datastructures::CellDescription(simplest_bound(proj, del.lower()->second), datastructures::Bound::infty);
    } else {
        return datastructures::CellDescription(simplest_bound(proj, del.lower()->second), simplest_bound(proj, del.upper()->second));
    }
}

template<typename T>
void compute_section_all_equational(datastructures::SampledDerivationRef<T>& der, datastructures::CellRepresentation<T>& response) {
    for (const auto& poly : der->delin().nullified()) {
        response.equational.insert(poly);
    }
    for (const auto& poly : der->delin().nonzero()) {
        response.equational.insert(poly);
    }
    for (const auto& [ran,irexprs] : der->delin().roots()) {
        for (const auto& ir : irexprs) {
            if (ir.index == 1 && ir.poly != response.description.section_defining().poly) { // add poly only once
                response.equational.insert(ir.poly);
            }
        }
    }
}

template <>
struct cell<CellHeuristic::BIGGEST_CELL> {
    template<typename T>
    static std::optional<datastructures::CellRepresentation<T>> compute(datastructures::SampledDerivationRef<T>& der) {
        datastructures::CellRepresentation<T> response(*der);
        response.description = compute_simplest_cell(der->proj(), der->cell());

        if (der->cell().is_section()) {
            compute_section_all_equational(der, response);
        } else { // sector
            if (!der->delin().nullified().empty()) return std::nullopt;

            if (!der->cell().lower_unbounded()) {
                auto it = der->cell().lower();
                while(true) {
                    for (const auto& ir : it->second) {
                        if (ir != *response.description.lower()) {
                            response.ordering.add_below(ir, *response.description.lower());
                        } 
                    }
                    if (it != der->delin().roots().begin()) it--;
                    else break;
                }
            }
            if (!der->cell().upper_unbounded()) {
                auto it = der->cell().upper();
                while(it != der->delin().roots().end()) {
                    for (const auto& ir : it->second) {
                        if (ir != *response.description.upper()) {
                            response.ordering.add_above(*response.description.upper(), ir);
                        }
                    }
                    it++;
                }
            }
        }
        return response;
    }
};

template <>
struct cell<CellHeuristic::CHAIN_EQ> {
    template<typename T>
    static std::optional<datastructures::CellRepresentation<T>> compute(datastructures::SampledDerivationRef<T>& der) {
        datastructures::CellRepresentation<T> response(*der);
        response.description = compute_simplest_cell(der->proj(), der->cell());

        if (der->cell().is_section()) {
            compute_section_all_equational(der, response);
        } else { // sector
            if (!der->delin().nullified().empty()) return std::nullopt;

            if (!der->cell().lower_unbounded()) {
                auto it = der->cell().lower();
                while(true) {
                    auto simplest = simplest_bound(der->proj(), it->second);
                    if (simplest != *response.description.lower()) {
                        response.ordering.add_below(simplest, *response.description.lower());
                    }
                    for (const auto& ir : it->second) {
                        if (ir != simplest) {
                            response.ordering.add_below(ir, simplest);
                        } 
                    }
                    if (it != der->delin().roots().begin()) it--;
                    else break;
                }
            }
            if (!der->cell().upper_unbounded()) {
                auto it = der->cell().upper();
                while(it != der->delin().roots().end()) {
                    auto simplest = simplest_bound(der->proj(), it->second);
                    if (simplest != *response.description.upper()) {
                        response.ordering.add_above(simplest, *response.description.upper());
                    }
                    for (const auto& ir : it->second) {
                        if (ir != simplest) {
                            response.ordering.add_above(ir, simplest);
                        } 
                    }
                    it++;
                }
            }
        }
        return response;
    }
};

}