"""Run all unit tests."""

# Copyright (C) 2006-2011 Anders Logg
#
# This file is part of DOLFIN.
#
# DOLFIN is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# DOLFIN is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with DOLFIN. If not, see <http://www.gnu.org/licenses/>.
#
# Modified by Johannes Ring 2009, 2011-2012
# Modified by Garth N. Wells 2009-2011
#
# First added:  2006-08-09
# Last changed: 2013-12-10

import sys, os, re
import platform
from instant import get_status_output
from dolfin import has_mpi, has_parmetis, has_scotch, has_linear_algebra_backend

# Tests to run
tests = {
    "adaptivity":     ["errorcontrol", "TimeSeries"],
    "ale":            ["HarmonicSmoothing"],
    "book":           ["chapter_1", "chapter_10"],
    "fem":            ["solving", "Assembler", "DirichletBC", "DofMap", \
                       "FiniteElement", "Form", "SystemAssembler",
                       "LocalSolver", "manifolds"],
    "function":       ["Constant", "ConstrainedFunctionSpace", \
                       "Expression", "Function", "FunctionAssigner", \
                       "FunctionSpace", "SpecialFunctions", \
                       "nonmatching_interpolation"],
    "geometry":       ["BoundingBoxTree", "Intersection", "Issues"],
    "graph":          ["GraphBuild"],
    "io":             ["vtk", "XMLMeshFunction", "XMLMesh", \
                       "XMLMeshValueCollection", "XMLVector", \
                       "XMLMeshData", "XMLLocalMeshData", \
                       "XDMF", "HDF5", "Exodus", "X3D"],
    "jit":            ["test"],
    "la":             ["test", "solve", "Matrix", "Scalar", "Vector", \
                       "KrylovSolver", "LinearOperator"],
    "math":           ["test"],
    "mesh":           ["Cell", "Edge", "Face", "MeshColoring", \
                       "MeshData", "MeshEditor", "MeshFunction", \
                       "MeshIterator", "MeshMarkers", "MeshQuality", \
                       "MeshValueCollection", "BoundaryMesh", "Mesh", \
                       "SubMesh", "MeshTransformation", "SubDomain", \
                       "PeriodicBoundaryComputation"],
    "meshconvert":    ["test"],
    "multistage":     ["RKSolver", "PointIntegralSolver"],
    "nls":            ["PETScSNESSolver", "TAOLinearBoundSolver"],
    "parameter":      ["Parameters"],
    "python-extras":  ["test"],
    "refinement":     ["refine"],
    }

# Run both C++ and Python tests as default
only_python = False

# Check if we should run only Python tests, use for quick testing
if len(sys.argv) == 2 and sys.argv[1] == "--only-python":
    only_python = True

# Build prefix list
prefixes = [""]
if has_mpi() and (has_parmetis() or has_scotch()) and \
       (has_linear_algebra_backend("Epetra") or \
        has_linear_algebra_backend("PETSc")):
    prefixes.append("mpirun -np 3 ")
else:
    print "DOLFIN has not been compiled with MPI and/or ParMETIS/SCOTCH. " \
          "Unit tests will not be run in parallel."

# Allow to disable parallel testing
if "DISABLE_PARALLEL_TESTING" in os.environ:
    prefixes = [""]

# Set non-interactive
os.putenv('DOLFIN_NOPLOT', '1')

failed = []
# Run tests in serial, then in parallel
for prefix in prefixes:
    for test, subtests in tests.items():
        for subtest in subtests:
            print "Running unit tests for %s (%s) with prefix '%s'" % (test,  subtest, prefix)
            print "----------------------------------------------------------------------"

            cpptest_executable = "test_" + subtest
            if platform.system() == 'Windows':
                cpptest_executable += '.exe'
            print "C++:   ",
            if only_python:
                print "Skipping tests as requested (--only-python)"
            elif not os.path.isfile(os.path.join(test, "cpp", cpptest_executable)):
                print "This test set does not have a C++ version"
            else:
                os.chdir(os.path.join(test, "cpp"))
                status, output = get_status_output("%s.%s%s" % \
                                   (prefix, os.path.sep, cpptest_executable))
                os.chdir(os.path.join(os.pardir, os.pardir))
                if status == 0 and "OK" in output:
                    print "OK",
                    match = re.search("OK \((\d+)\)", output)
                    if match:
                        num_tests = int(match.groups()[0])
                        print "(%d tests)" % num_tests,
                    print
                else:
                    print "*** Failed"
                    failed += [(test, subtest, "C++", output)]

            print "Python:",
            if os.path.isfile(os.path.join(test, "python", subtest + ".py")):
                os.chdir(os.path.join(test,"python"))
                status, output = get_status_output("%s%s .%s%s.py" % \
                                   (prefix, sys.executable, os.path.sep, subtest))
                os.chdir(os.path.join(os.pardir, os.pardir))
                if status == 0 and "OK" in output:
                    print "OK",
                    match = re.search("Ran (\d+) test", output)
                    if match:
                        num_tests = int(match.groups()[0])
                        print "(%d tests)" % num_tests,
                    print
                else:
                    print "*** Failed"
                    failed += [(test, subtest, "Python", output)]
            else:
                print "Skipping"

            print ""

    # Print output for failed tests
    for (test, subtest, interface, output) in failed:
        print "One or more unit tests failed for %s (%s, %s):" % (test, subtest, interface)
        print output
        open("fail.log", "w").write(output)

# Return error code if tests failed
sys.exit(len(failed) != 0)
