#!/bin/bash

gawk '
# the following block processes all the lines
# and populates counters and values
{
	if ($7 == "diff:") {
    if($2 in counters) {
        counters[$2]++;
    } else {
        counters[$2] = 1;
    }
    i = counters[$2];
    values[$2, i] = $8;
	}
} END {
        printf "\tmin\tavg\tmax\tcount\t\n"; 
    for (c in counters) {
        delete tmp;
        min = values[c, 1];
        max = values[c, 1];
        sum = values[c, 1];
        tmp[1] = values[c, 1];
        for (i = 2; i <= counters[c]; i++) {
            if (values[c, i] < min) min = values[c, i];
            if (values[c, i] > max) max = values[c, i];
            sum += values[c, i];
            tmp[i] = values[c, i];
        }

        # Output of the statistics for this group.  
        printf "%s\t%d\t%f\t%d\t%d\n", c, min, (sum / counters[c]),  max, counters[c]; 
    }
}'
