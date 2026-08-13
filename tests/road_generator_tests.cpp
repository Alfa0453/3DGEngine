#include "RoadGeneratorPanel.h"
#include <cstdlib>
#include <iostream>
namespace { void Require(bool value,const char* message){if(!value){std::cerr<<"FAILED: "<<message<<'\n';std::exit(1);}} }
int main(){
    RoadGeneratorPanel road;const std::vector<glm::vec3> line{{0,0,0},{0,0,10}};
    auto parts=road.GenerateParts(line,false);Require(parts.size()==30,"default road piece count");
    int roads=0,shoulders=0,marks=0;for(const auto& p:parts){roads+=p.surface==RoadGeneratorPanel::Surface::Road;shoulders+=p.surface==RoadGeneratorPanel::Surface::Shoulder;marks+=p.surface==RoadGeneratorPanel::Surface::Marking;}
    Require(roads==9,"surface spans and caps");Require(shoulders==14,"two shoulders per span");Require(marks==7,"one divider per span");
    road.SetShoulders(false);road.SetMarkings(false);Require(road.GenerateParts(line,false).size()==9,"minimal road");
    Require(road.GenerateParts({},false).empty(),"empty spline");std::cout<<"Road generator tests passed\n";return 0;
}
