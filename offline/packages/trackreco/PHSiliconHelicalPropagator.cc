#include "PHSiliconHelicalPropagator.h"

#include <trackbase_historic/TrackSeed_v1.h>
#include <trackbase_historic/TrackSeedContainer_v1.h>
#include <trackbase_historic/SvtxTrackSeed_v1.h>

#include <fun4all/Fun4AllReturnCodes.h>
#include <phool/getClass.h>
#include <phool/PHCompositeNode.h>

PHSiliconHelicalPropagator::PHSiliconHelicalPropagator(std::string name) : SubsysReco(name)
{
  _fitter = new HelicalFitter();
}

PHSiliconHelicalPropagator::~PHSiliconHelicalPropagator()
{
  delete _fitter;
}

int PHSiliconHelicalPropagator::InitRun(PHCompositeNode* topNode)
{
  _fitter->InitRun(topNode);

  _cluster_map = findNode::getClass<TrkrClusterContainer>(topNode, "TRKR_CLUSTER");
  if (!_cluster_map)
  {
    std::cerr << PHWHERE << " ERROR: Can't find node TRKR_CLUSTER" << std::endl;
    return Fun4AllReturnCodes::ABORTEVENT;
  }
  _tgeometry = findNode::getClass<ActsGeometry>(topNode, "ActsGeometry");
  if(!_tgeometry)
  {
    std::cout << "No Acts tracking geometry, exiting." << std::endl;
    return Fun4AllReturnCodes::ABORTEVENT;
  }
  _tpc_seeds = findNode::getClass<TrackSeedContainer>(topNode,"TpcTrackSeedContainer");
  if(!_tpc_seeds)
  {
    std::cout << "No TpcTrackSeedContainer, exiting." << std::endl;
    return Fun4AllReturnCodes::ABORTEVENT;
  }
  _si_seeds = findNode::getClass<TrackSeedContainer>(topNode,"SiliconTrackSeedContainer");
  if(!_si_seeds)
  {
    std::cout << "No SiliconTrackSeedContainer, creating..." << std::endl;
    if(createSeedContainer("SiliconTrackSeedContainer", topNode) != Fun4AllReturnCodes::EVENT_OK)
    {
      std::cout << "Cannot create, exiting." << std::endl;
      return Fun4AllReturnCodes::ABORTEVENT; 
    }
  }
  _svtx_seeds = findNode::getClass<TrackSeedContainer>(topNode,_track_map_name);
  if(!_svtx_seeds)
  {
    std::cout << "No " << _track_map_name << " found, creating..." << std::endl;
    if(createSeedContainer(_track_map_name, topNode) != Fun4AllReturnCodes::EVENT_OK)
    {
      std::cout << "Cannot create, exiting." << std::endl;
      return Fun4AllReturnCodes::ABORTEVENT;
    }
  }
  return Fun4AllReturnCodes::EVENT_OK;
}

int PHSiliconHelicalPropagator::createSeedContainer(std::string container_name, PHCompositeNode *topNode)
{
  PHNodeIterator iter(topNode);

  PHCompositeNode *dstNode = dynamic_cast<PHCompositeNode*>(iter.findFirst("PHCompositeNode", "DST"));

  if (!dstNode)
  {
    std::cerr << "DST node is missing, quitting" << std::endl;
    throw std::runtime_error("Failed to find DST node in PHSiliconHelicalPropagator::createNodes");
  }

  PHNodeIterator dstIter(dstNode);
  PHCompositeNode *svtxNode = dynamic_cast<PHCompositeNode *>(dstIter.findFirst("PHCompositeNode", "SVTX"));

  if (!svtxNode)
  {
    svtxNode = new PHCompositeNode("SVTX");
    dstNode->addNode(svtxNode);
  }

  TrackSeedContainer *m_seedContainer = findNode::getClass<TrackSeedContainer>(topNode, container_name);
  if(!m_seedContainer)
  {
    m_seedContainer = new TrackSeedContainer_v1;
    PHIODataNode<PHObject> *trackNode = new PHIODataNode<PHObject>(m_seedContainer, container_name, "PHObject");
    svtxNode->addNode(trackNode);
  }
  
  return Fun4AllReturnCodes::EVENT_OK;
}

int PHSiliconHelicalPropagator::process_event(PHCompositeNode* /*topNode*/)
{
  for(TrackSeedContainer::ConstIter seed_iter = _tpc_seeds->begin(); seed_iter != _tpc_seeds->end(); ++seed_iter)
  {
    TrackSeed* tpcseed = *seed_iter;
    if(!tpcseed) continue;

    std::vector<Acts::Vector3> clusterPositions;
    std::vector<TrkrDefs::cluskey> clusterKeys;
    _fitter->getTrackletClusters(tpcseed,clusterPositions,clusterKeys);
    std::vector<float> fitparams = _fitter->fitClusters(clusterPositions,clusterKeys);
    
    std::vector<TrkrDefs::cluskey> si_clusterKeys;
    std::vector<Acts::Vector3> si_clusterPositions;
    unsigned int nSiClusters = _fitter->addSiliconClusters(fitparams,si_clusterPositions,si_clusterKeys);
    
    std::unique_ptr<TrackSeed_v1> si_seed = std::make_unique<TrackSeed_v1>();
    for(auto clusterkey : si_clusterKeys)
    {
      si_seed->insert_cluster_key(clusterkey);
    }
    si_seed->circleFitByTaubin(_cluster_map,_tgeometry,0,8);
    si_seed->lineFit(_cluster_map,_tgeometry,0,8);
    
    TrackSeed* mapped_seed = _si_seeds->insert(si_seed.get());

    std::unique_ptr<SvtxTrackSeed_v1> full_seed = std::make_unique<SvtxTrackSeed_v1>();
    int tpc_seed_index = _tpc_seeds->index(seed_iter);
    int si_seed_index = _si_seeds->find(mapped_seed);
    if(Verbosity()>0)
    {
      std::cout << "inserted " << nSiClusters << " silicon clusters for tpc seed " << tpc_seed_index << std::endl;
      std::cout << "new silicon seed index: " << si_seed_index << std::endl;
    }
    full_seed->set_tpc_seed_index(tpc_seed_index);
    full_seed->set_silicon_seed_index(si_seed_index);
    _svtx_seeds->insert(full_seed.get());
  }
  return Fun4AllReturnCodes::EVENT_OK;
}

int PHSiliconHelicalPropagator::End(PHCompositeNode* /*topNode*/)
{
  return Fun4AllReturnCodes::EVENT_OK;
}
