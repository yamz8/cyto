#pragma once

// Made-up FCS samples for the tests: two scatter clusters, and inside the
// small one a CD4/CD8 split, with spillover from FL1 into FL2.

#include <QFile>
#include <QString>
#include <random>

#include "fcs.h"

inline Spillover testSpillover() {
    Spillover spillover;
    spillover.channels = {"FL1-A", "FL2-A"};
    spillover.matrix = {1, 0.2, 0, 1};
    return spillover;
}

// small: events in the small scatter cluster (FSC ≈ 50K), of which
// cd4Share are FL1-high and the rest FL2-high. The others sit at FSC ≈ 150K.
inline FcsSample testSample(const QString &name, int small, int large, double cd4Share, unsigned seed = 1) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0, 1);
    FcsSample sample;
    sample.name = name;
    sample.channels = {"FSC-A", "SSC-A", "FL1-A", "FL2-A"};
    sample.labels = {"", "", "CD4", "CD8"};
    sample.ranges = {262144, 262144, 262144, 262144};
    sample.spillover = testSpillover();
    sample.events = small + large;
    const int cd4 = int(small * cd4Share + 0.5);
    for (int e = 0; e < sample.events; ++e) {
        const bool isSmall = e < small;
        const double fsc = (isSmall ? 50000 : 150000) + 3000 * noise(rng);
        const double ssc = (isSmall ? 20000 : 100000) + 3000 * noise(rng);
        double fl1 = 50 + 20 * noise(rng), fl2 = 50 + 20 * noise(rng);
        if (isSmall && e < cd4)
            fl1 = 10000 * std::exp(0.2 * noise(rng));
        else if (isSmall)
            fl2 = 8000 * std::exp(0.2 * noise(rng));
        // Spillover: observed = true × matrix.
        const double observed1 = fl1, observed2 = fl2 + 0.2 * fl1;
        for (double value : {fsc, ssc, observed1, observed2})
            sample.data.push_back(float(value));
    }
    return sample;
}

inline QString writeTestSample(const QString &path, const FcsSample &sample) {
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(Fcs::write(sample));
    return path;
}
