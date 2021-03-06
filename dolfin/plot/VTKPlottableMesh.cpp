// Copyright (C) 2012 Fredrik Valdmanis
//
// This file is part of DOLFIN.
//
// DOLFIN is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// DOLFIN is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with DOLFIN. If not, see <http://www.gnu.org/licenses/>.
//
// Modified by Joachim B Haga 2012
// Modified by Benjamin Kehlet 2012
//
// First added:  2012-06-20
// Last changed: 2013-11-15

#ifdef HAS_VTK

#include <vtkCellArray.h>
#include <vtkCellCenters.h>
#include <vtkCellData.h>
#include <vtkFloatArray.h>
#include <vtkGeometryFilter.h>
#include <vtkIdFilter.h>
#include <vtkPointData.h>
#include <vtkPointSetAlgorithm.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkSelectVisiblePoints.h>
#include <vtkStringArray.h>
#include <vtkTextProperty.h>
#include <vtkUnstructuredGrid.h>
#include <vtkVectorNorm.h>

#if (VTK_MAJOR_VERSION == 6) || ((VTK_MAJOR_VERSION == 5) && (VTK_MINOR_VERSION >= 4))
#include <vtkLabeledDataMapper.h>
#include <vtkPointSetToLabelHierarchy.h>
#endif

#include <dolfin/common/Timer.h>
#include <dolfin/mesh/Vertex.h>
#include <dolfin/mesh/CellType.h>

#include "VTKWindowOutputStage.h"
#include "VTKPlottableMesh.h"

using namespace dolfin;

//----------------------------------------------------------------------------
VTKPlottableMesh::VTKPlottableMesh(boost::shared_ptr<const Mesh> mesh, std::size_t entity_dim) :
  _grid(vtkSmartPointer<vtkUnstructuredGrid>::New()),
  _full_grid(vtkSmartPointer<vtkUnstructuredGrid>::New()),
  _geometryFilter(vtkSmartPointer<vtkGeometryFilter>::New()),
  _mesh(mesh),
  _entity_dim(entity_dim)
{
  // Do nothing
}
//----------------------------------------------------------------------------
VTKPlottableMesh::VTKPlottableMesh(boost::shared_ptr<const Mesh> mesh) :
  _grid(vtkSmartPointer<vtkUnstructuredGrid>::New()),
  _full_grid(vtkSmartPointer<vtkUnstructuredGrid>::New()),
  _geometryFilter(vtkSmartPointer<vtkGeometryFilter>::New()),
  _mesh(mesh),
  _entity_dim(mesh->topology().dim())
{
  // Do nothing
}
//----------------------------------------------------------------------------
void VTKPlottableMesh::init_pipeline(const Parameters& p)
{
  dolfin_assert(_geometryFilter);

  #if VTK_MAJOR_VERSION <= 5
  _geometryFilter->SetInput(_grid);
  #else
  _geometryFilter->SetInputData(_grid);
  #endif
  _geometryFilter->Update();
}
//----------------------------------------------------------------------------
void VTKPlottableMesh::connect_to_output(VTKWindowOutputStage& output)
{
  bool has_nans = false;

  vtkFloatArray* pointdata
    = dynamic_cast<vtkFloatArray*>(_grid->GetPointData()->GetScalars());
  if (pointdata && pointdata->GetNumberOfComponents() == 1)
  {
    for (int i = 0; i < pointdata->GetNumberOfTuples(); i++)
    {
      if (std::isnan(pointdata->GetValue(i)))
      {
        has_nans = true;
        break;
      }
    }
  }

  vtkFloatArray *celldata = dynamic_cast<vtkFloatArray*>(_grid->GetCellData()->GetScalars());
  if (celldata && celldata->GetNumberOfComponents() == 1 && !has_nans)
  {
    for (int i = 0; i < celldata->GetNumberOfTuples(); i++)
    {
      if (std::isnan(celldata->GetValue(i)))
      {
        has_nans = true;
        break;
      }
    }
  }

  output.set_translucent(has_nans, _entity_dim, dim());
  output.set_input(get_output());
}
//----------------------------------------------------------------------------
vtkSmartPointer<vtkAlgorithmOutput> VTKPlottableMesh::get_output() const
{
  return _geometryFilter->GetOutputPort();
}
//----------------------------------------------------------------------------
bool VTKPlottableMesh::is_compatible(const Variable &var) const
{
  return dynamic_cast<const Mesh*>(&var);
}
//----------------------------------------------------------------------------
void VTKPlottableMesh::update(boost::shared_ptr<const Variable> var,
                              const Parameters& p, int framecounter)
{
  if (var)
    _mesh = boost::dynamic_pointer_cast<const Mesh>(var);

  dolfin_assert(_grid);
  dolfin_assert(_full_grid);
  dolfin_assert(_mesh);

  Timer t("VTK construct grid");

  //
  // Construct VTK point array from DOLFIN mesh vertices
  //

  // Create point array
  vtkSmartPointer<vtkPoints> points = vtkSmartPointer<vtkPoints>::New();
  points->SetNumberOfPoints(_mesh->num_vertices());

  if(_mesh->topology().dim() == 1)
  {
    // HACK to sort 1D points into ascending order
    // because vtkXYPlotActor does not recognise
    // cell connectivity information
    std::vector<double> pointx;
    for (VertexIterator vertex(*_mesh); !vertex.end(); ++vertex)
    {
      const Point point = vertex->point();
      pointx.push_back(point.x());
    }
    std::sort(pointx.begin(), pointx.end());
    for(std::vector<double>::iterator p=pointx.begin(); p!=pointx.end(); ++p)
      points->SetPoint(p - pointx.begin(), *p, 0.0, 0.0);
  }
  else
  {
    // Iterate vertices and add to point array
    for (VertexIterator vertex(*_mesh); !vertex.end(); ++vertex)
    {
      const Point point = vertex->point();
      points->SetPoint(vertex->index(), point.x(), point.y(), point.z());
    }
  }
  
  // Insert points, vertex labels and cells in VTK unstructured grid
  _full_grid->SetPoints(points);

  //
  // Construct VTK cells from DOLFIN facets
  //

  build_grid_cells(_full_grid, _mesh->topology().dim());
  if (_entity_dim == dim())
    _grid->ShallowCopy(_full_grid);
  else
  {
    _grid->SetPoints(points);
    build_grid_cells(_grid, _entity_dim);
  }
}
//----------------------------------------------------------------------------
void VTKPlottableMesh::build_grid_cells(vtkSmartPointer<vtkUnstructuredGrid>& grid,
                                        std::size_t topological_dim)
{
  // Add mesh cells to VTK cell array. Note: Preallocation of storage
  // in cell array did not give speedups when testing during development
  vtkSmartPointer<vtkCellArray> cells = vtkSmartPointer<vtkCellArray>::New();

  _mesh->init(topological_dim, 0);
  const std::vector<unsigned int>& connectivity = _mesh->topology()(topological_dim, 0)();

  for (std::size_t i = 0; i < _mesh->size(topological_dim); ++i)
  {
    // Insert all vertex indices for a given cell. For a simplex cell in nD,
    // n+1 indices are inserted. The connectivity array must be indexed at
    // ((n+1) x cell_number + idx_offset)
    cells->InsertNextCell(topological_dim + 1);
    for(std::size_t j = 0; j <= topological_dim; ++j)
      cells->InsertCellPoint(connectivity[(topological_dim + 1)*i + j]);
  }

  // Free unused memory in cell array
  // (automatically allocated during cell insertion)
  cells->Squeeze();

  switch (topological_dim)
  {
    case 0:
      grid->SetCells(VTK_VERTEX, cells);
      break;
    case 1:
      grid->SetCells(VTK_LINE, cells);
      break;
    case 2:
      grid->SetCells(VTK_TRIANGLE, cells);
      break;
    case 3:
      grid->SetCells(VTK_TETRA, cells);
      break;
    default:
      dolfin_error("VTKPlottableMesh.cpp", "initialise cells", "Not implemented for dim>3");
      break;
  }
}
//----------------------------------------------------------------------------
void VTKPlottableMesh::update_range(double range[2])
{
  _grid->GetScalarRange(range);
}
//----------------------------------------------------------------------------
std::size_t VTKPlottableMesh::dim() const
{
  return _mesh->geometry().dim();
}
//----------------------------------------------------------------------------
void VTKPlottableMesh::build_id_filter()
{
  if (!_idFilter)
  {
    _idFilter = vtkSmartPointer<vtkIdFilter>::New();
    if (_entity_dim == dim() || _entity_dim == 0)
    {
      // Use the un-warped mesh if dimension is full. If dim is zero, use the
      // original cells rather than vertices (the vertices are labeled by the
      // vertex label actor anyway).
      _idFilter->SetInputConnection(get_mesh_actor()->GetMapper()->GetInputConnection(0,0));
    }
    else
      _idFilter->SetInputConnection(_geometryFilter->GetOutputPort());

    _idFilter->PointIdsOn();
    _idFilter->CellIdsOn();
    _idFilter->FieldDataOn();
  }
}
//----------------------------------------------------------------------------
vtkSmartPointer<vtkActor2D> VTKPlottableMesh::get_vertex_label_actor(vtkSmartPointer<vtkRenderer> renderer)
{
#if (VTK_MAJOR_VERSION == 6) || ((VTK_MAJOR_VERSION == 5) && (VTK_MINOR_VERSION >= 4))
  // Return actor if already created
  if (!_vertexLabelActor)
  {
    build_id_filter();

    vtkSmartPointer<vtkSelectVisiblePoints> vis = vtkSmartPointer<vtkSelectVisiblePoints>::New();
    vis->SetInputConnection(_idFilter->GetOutputPort());
    // If the tolerance is too high, too many labels are visible (especially at
    // a distance).  If set too low, some labels are invisible. There isn't a
    // "correct" value, it should really depend on distance and resolution.
    vis->SetTolerance(1e-3);
    vis->SetRenderer(renderer);
    //vis->SelectionWindowOn();
    //vis->SetSelection(0, 0.3, 0, 0.3);

    vtkSmartPointer<vtkLabeledDataMapper> ldm = vtkSmartPointer<vtkLabeledDataMapper>::New();
    ldm->SetInputConnection(vis->GetOutputPort());
    ldm->SetLabelModeToLabelFieldData();
    ldm->GetLabelTextProperty()->SetColor(0.0, 0.0, 0.0);
    ldm->GetLabelTextProperty()->ItalicOff();
    ldm->GetLabelTextProperty()->ShadowOff();

    _vertexLabelActor = vtkSmartPointer<vtkActor2D>::New();
    _vertexLabelActor->SetMapper(ldm);
  }
  #else
  warning("Plotting of vertex labels requires VTK >= 5.4");
  #endif

  return _vertexLabelActor;
}
//----------------------------------------------------------------------------
vtkSmartPointer<vtkActor2D> VTKPlottableMesh::get_cell_label_actor(vtkSmartPointer<vtkRenderer> renderer)
{
  #if (VTK_MAJOR_VERSION == 6) || ((VTK_MAJOR_VERSION == 5) && (VTK_MINOR_VERSION >= 4))
  if (!_cellLabelActor)
  {
    build_id_filter();

    vtkSmartPointer<vtkCellCenters> cc = vtkSmartPointer<vtkCellCenters>::New();
    cc->SetInputConnection(_idFilter->GetOutputPort());

    vtkSmartPointer<vtkSelectVisiblePoints> vis = vtkSmartPointer<vtkSelectVisiblePoints>::New();
    vis->SetTolerance(1e-4); // See comment for vertex labels
    vis->SetInputConnection(cc->GetOutputPort());
    vis->SetRenderer(renderer);

    vtkSmartPointer<vtkLabeledDataMapper> ldm = vtkSmartPointer<vtkLabeledDataMapper>::New();
    ldm->SetInputConnection(vis->GetOutputPort());
    ldm->SetLabelModeToLabelFieldData();
    ldm->GetLabelTextProperty()->SetColor(0.3, 0.3, 0.0);
    ldm->GetLabelTextProperty()->ShadowOff();

    _cellLabelActor = vtkSmartPointer<vtkActor2D>::New();
    _cellLabelActor->SetMapper(ldm);
  }
  #else
  warning("Plotting of cell labels requires VTK >= 5.4");
  #endif

  return _cellLabelActor;
}
//----------------------------------------------------------------------------
vtkSmartPointer<vtkActor> VTKPlottableMesh::get_mesh_actor()
{
  if (!_meshActor)
  {
    vtkSmartPointer<vtkGeometryFilter> geomfilter = vtkSmartPointer<vtkGeometryFilter>::New();
    #if VTK_MAJOR_VERSION <= 5
    geomfilter->SetInput(_full_grid);
    #else
    geomfilter->SetInputData(_full_grid);
    #endif
    geomfilter->Update();

    vtkSmartPointer<vtkPolyDataMapper> mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(geomfilter->GetOutputPort());

    _meshActor = vtkSmartPointer<vtkActor>::New();
    _meshActor->SetMapper(mapper);
    _meshActor->GetProperty()->SetRepresentationToWireframe();
    _meshActor->GetProperty()->SetColor(0.7, 0.7, 0.3);
    _meshActor->GetProperty()->SetOpacity(0.5);
    vtkMapper::SetResolveCoincidentTopologyToPolygonOffset();
  }
  return _meshActor;
}
//----------------------------------------------------------------------------
boost::shared_ptr<const Mesh> VTKPlottableMesh::mesh() const
{
  return _mesh;
}
//----------------------------------------------------------------------------
vtkSmartPointer<vtkUnstructuredGrid> VTKPlottableMesh::grid() const
{
  return _grid;
}
//----------------------------------------------------------------------------
void VTKPlottableMesh::insert_filter(vtkSmartPointer<vtkPointSetAlgorithm> filter)
{
  if (filter)
  {
    #if VTK_MAJOR_VERSION <= 5
    filter->SetInput(_grid);
    _geometryFilter->SetInput(filter->GetOutput());
    #else
    filter->SetInputData(_grid);
    _geometryFilter->SetInputData(filter->GetOutput());
    #endif
  }
  else
  {
    #if VTK_MAJOR_VERSION <= 5
    _geometryFilter->SetInput(_grid);
    #else
    _geometryFilter->SetInputData(_grid);
    #endif
  }

  _geometryFilter->Update();
}
//----------------------------------------------------------------------------
VTKPlottableMesh *dolfin::CreateVTKPlottable(boost::shared_ptr<const Mesh> mesh)
{
  return new VTKPlottableMesh(mesh);
}
//---------------------------------------------------------------------------
void VTKPlottableMesh::filter_scalars(vtkFloatArray *values,
                                      const Parameters& p)
{
  dolfin_assert(values);

  const Parameter &param_hide_below = p["hide_below"];
  const Parameter &param_hide_above = p["hide_above"];
  if (param_hide_below.is_set() || param_hide_above.is_set())
  {
    float hide_above =  std::numeric_limits<float>::infinity();
    float hide_below = -std::numeric_limits<float>::infinity();
    if (param_hide_below.is_set()) hide_below = (double)param_hide_below;
    if (param_hide_above.is_set()) hide_above = (double)param_hide_above;

    const std::size_t num_tuples = static_cast<std::size_t>(values->GetNumberOfTuples());
    for (std::size_t i = 0; i < num_tuples; i++)
    {
      float val = values->GetValue(i);
      if (val < hide_below || val > hide_above)
        values->SetValue(i, std::numeric_limits<float>::quiet_NaN());
    }
  }
}
//---------------------------------------------------------------------------
template <class T>
void VTKPlottableMesh::setPointValues(std::size_t size, const T* indata,
                                      const Parameters& p)
{
  const std::size_t num_vertices = _mesh->num_vertices();
  const std::size_t num_components = size / num_vertices;

  dolfin_assert(num_components > 0 && num_components <= 3);
  dolfin_assert(num_vertices*num_components == size);

  vtkSmartPointer<vtkFloatArray> values =
    vtkSmartPointer<vtkFloatArray>::New();
  if (num_components == 1)
  {
    values->SetNumberOfValues(num_vertices);
    for (std::size_t i = 0; i < num_vertices; ++i)
      values->SetValue(i, (double)indata[i]);

    filter_scalars(values, p);

    _grid->GetPointData()->SetScalars(values);
  }
  else
  {
    // NOTE: Allocation must be done in this order!
    // Note also that the number of VTK vector components must always be 3
    // regardless of the function vector value dimension
    values->SetNumberOfComponents(3);
    values->SetNumberOfTuples(num_vertices);
    for (std::size_t i = 0; i < num_vertices; ++i)
    {
      // The entries in "vertex_values" must be copied to "vectors". Viewing
      // these arrays as matrices, the transpose of vertex values should be copied,
      // since DOLFIN and VTK store vector function values differently
      for (std::size_t d = 0; d < num_components; d++)
        values->SetValue(3*i+d, indata[i+num_vertices*d]);
      for (std::size_t d = num_components; d < 3; d++)
        values->SetValue(3*i+d, 0.0);
    }
    _grid->GetPointData()->SetVectors(values);

    // Compute norms of vector data
    vtkSmartPointer<vtkVectorNorm>
      norms =vtkSmartPointer<vtkVectorNorm>::New();
    #if VTK_MAJOR_VERSION <= 5
    norms->SetInput(_grid);
    #else
    norms->SetInputData(_grid);
    #endif
    norms->SetAttributeModeToUsePointData();
    //NOTE: This update is necessary to actually compute the norms
    norms->Update();

    // Attach vector norms as scalar point data in the VTK grid
    _grid->GetPointData()->SetScalars(norms->GetOutput()->GetPointData()->GetScalars());
  }
}
//----------------------------------------------------------------------------
template <class T>
void VTKPlottableMesh::setCellValues(std::size_t size, const T* indata,
                                     const Parameters& p)
{
  const std::size_t num_entities = _mesh->num_entities(_entity_dim);
  dolfin_assert(num_entities == size);

  vtkSmartPointer<vtkFloatArray> values =
    vtkSmartPointer<vtkFloatArray>::New();
  values->SetNumberOfValues(num_entities);

  for (std::size_t i = 0; i < num_entities; ++i)
    values->SetValue(i, (float)indata[i]);

  filter_scalars(values, p);
  _grid->GetCellData()->SetScalars(values);
}

//----------------------------------------------------------------------------
// Instantiate function templates for valid types
//----------------------------------------------------------------------------

#define INSTANTIATE(T)                                                  \
  template void dolfin::VTKPlottableMesh::setPointValues(std::size_t, const T*, const Parameters&); \
  template void dolfin::VTKPlottableMesh::setCellValues(std::size_t, const T*, const Parameters&);

INSTANTIATE(bool)
INSTANTIATE(double)
INSTANTIATE(float)
INSTANTIATE(int)

// Handle std::size_t and std::size_t. See note in VTKPlottableMeshFunction.cpp for explanation
INSTANTIATE(unsigned int)
INSTANTIATE(unsigned long)

#endif
