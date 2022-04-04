#pragma once

#include "filter.h"

class YUYVtoRGB32 : public Filter
{
public:
  YUYVtoRGB32(QString id, StatisticsInterface *stats,
               std::shared_ptr<ResourceAllocator> hwResources);

protected:
  void process();
};

