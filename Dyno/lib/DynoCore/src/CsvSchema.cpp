#include "CsvSchema.h"

#include "LineBuilder.h"

bool formatCsvHeader(OutputLine& line) {
    LineBuilder b(line);
    for (size_t i = 0; i < kCsvColumnCount; ++i) {
        if (i) b.append(",");
        b.append("%s", kCsvColumns[i].name);
    }
    b.append("\r\n");
    return b.finish();
}
