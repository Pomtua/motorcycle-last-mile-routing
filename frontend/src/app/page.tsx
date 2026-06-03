"use client";

import dynamic from "next/dynamic";
import { useState, useRef, useEffect } from "react";
import { Upload } from "lucide-react";

const MapView = dynamic(() => import("../components/Map"), { ssr: false });

const defaultDepot = { lat: 14.075396, lng: 100.597351 };

const defaultParcels = [
  { id: 6, lat: 13.788422, lng: 100.665142, weight: 1.94, volume: 8.36, time_window: [584, 764] },
  { id: 10, lat: 13.657918, lng: 100.348584, weight: 2.73, volume: 16.22, time_window: [707, 887] },
  { id: 15, lat: 13.776304, lng: 100.55707, weight: 3.13, volume: 16.48, time_window: [487, 667] },
  { id: 41, lat: 13.720701, lng: 100.536697, weight: 2.25, volume: 9.37, time_window: [515, 695] },
  { id: 42, lat: 13.720595, lng: 100.536423, weight: 1.5, volume: 6.39, time_window: [535, 715] },
  { id: 4, lat: 13.720081, lng: 100.535203, weight: 3.15, volume: 18.53, time_window: [532, 712] },
  { id: 45, lat: 13.82078, lng: 100.748999, weight: 3.76, volume: 17.72, time_window: [594, 774] },
  { id: 12, lat: 13.658394, lng: 100.3481, weight: 1.39, volume: 6.85, time_window: [720, 900] },
  { id: 48, lat: 13.783501, lng: 100.799468, weight: 3.15, volume: 13.26, time_window: [705, 885] },
  { id: 31, lat: 13.778618, lng: 100.509201, weight: 1.57, volume: 8.72, time_window: [601, 781] }
];

export default function Home() {
  const [activeTab, setActiveTab] = useState("Simulation Map");
  const [algorithm, setAlgorithm] = useState("C++ Engine");
  const [drivers, setDrivers] = useState(5);
  const [maxWeight, setMaxWeight] = useState(20);
  const [maxVolume, setMaxVolume] = useState(50);
  const [showZone, setShowZone] = useState(false);

  const [depot, setDepot] = useState(defaultDepot);
  const [parcels, setParcels] = useState<any[]>(defaultParcels);
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [allResults, setAllResults] = useState<{ [key: string]: any }>({});
  const [activeResult, setActiveResult] = useState<any>(null);
  const [selectedDriverIndex, setSelectedDriverIndex] = useState(0);
  const [driverMetricsPage, setDriverMetricsPage] = useState(0);
  const [sequencePage, setSequencePage] = useState(0);

  const ITEMS_PER_PAGE = 5;

  useEffect(() => {
    if (activeResult && activeResult.routes) {
      if (selectedDriverIndex >= activeResult.routes.length) {
        setSelectedDriverIndex(Math.max(0, activeResult.routes.length - 1));
      }
    } else {
      setSelectedDriverIndex(0);
    }
  }, [activeResult, selectedDriverIndex]);

  useEffect(() => {
    setDriverMetricsPage(0);
    setSequencePage(0);
  }, [activeResult]);

  const [datasetCategory, setDatasetCategory] = useState("scale");
  const [availableInstances, setAvailableInstances] = useState<{ [key: string]: string[] }>({
    scale: [],
    split_capability: [],
    tightness: []
  });
  const fileInputRef = useRef<HTMLInputElement>(null);

  const [isBatchMode, setIsBatchMode] = useState(false);
  const [batchRunning, setBatchRunning] = useState(false);
  const [batchProgress, setBatchProgress] = useState({ current: 0, total: 0, activeInstance: "" });
  const [benchmarkData, setBenchmarkData] = useState<any[]>([]);
  const [analyticsCategoryFilter, setAnalyticsCategoryFilter] = useState("All");
  const [masterTablePage, setMasterTablePage] = useState(0);
  const [isProgressMinimized, setIsProgressMinimized] = useState(false);


  const loadPrecomputedData = async () => {
    try {
      const res = await fetch("http://localhost:3000/simulations/benchmark/precomputed");
      if (res.ok) {
        const data = await res.json();
        setBenchmarkData(data);
      }
    } catch (err) {
      console.error(err);
    }
  };

  useEffect(() => {
    loadPrecomputedData();
  }, []);

  const startBatchSimulation = async () => {
    setBatchRunning(true);
    setIsProgressMinimized(false);
    setBatchProgress({ current: 0, total: 0, activeInstance: "Starting..." });
    setError(null);
    try {
      const res = await fetch("http://localhost:3000/simulations/benchmark", { method: "POST" });
      if (!res.ok) throw new Error("Failed to start batch");
      const pollTimer = setInterval(async () => {
        try {
          const statusRes = await fetch("http://localhost:3000/simulations/benchmark/status");
          if (statusRes.ok) {
            const status = await statusRes.json();
            if (status.running) {
              setBatchProgress(status.progress);
            } else {
              clearInterval(pollTimer);
              setBatchRunning(false);
              setIsProgressMinimized(false);
              loadPrecomputedData();
            }
          }
        } catch (err) {
          console.error(err);
        }
      }, 500);
    } catch (err: any) {
      setError(err.message || "Failed to start batch");
      setBatchRunning(false);
    }
  };

  const stopBatchSimulation = async () => {
    try {
      const res = await fetch("http://localhost:3000/simulations/benchmark/stop", { method: "POST" });
      if (res.ok) {
        setBatchRunning(false);
        setIsProgressMinimized(false);
        loadPrecomputedData();
      }
    } catch (err) {
      console.error(err);
    }
  };

  const exportCSV = () => {
    if (benchmarkData.length === 0) return;
    const headers = Object.keys(benchmarkData[0]).join(",");
    const rows = benchmarkData.map(row => 
      Object.values(row).map(val => typeof val === "string" && val.includes(",") ? `"${val}"` : val).join(",")
    );
    const csvContent = "data:text/csv;charset=utf-8," + [headers, ...rows].join("\n");
    const encodedUri = encodeURI(csvContent);
    const link = document.createElement("a");
    link.setAttribute("href", encodedUri);
    link.setAttribute("download", "benchmark_report.csv");
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  };

  const [selectedInstanceFilename, setSelectedInstanceFilename] = useState("");

  const tabs = ["Simulation Map", "Fleet Schedule", "Analytics"];

  useEffect(() => {
    fetch("http://localhost:3000/instances")
      .then(res => res.json())
      .then(data => {
        setAvailableInstances(data);
        if (data.scale && data.scale.length > 0) {
          setSelectedInstanceFilename(data.scale[0]);
          handleLoadInstance("scale", data.scale[0]);
        }
      })
      .catch(err => console.error(err));
  }, []);

  const handleUploadClick = () => {
    fileInputRef.current?.click();
  };

  const handleFileChange = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;

    const formData = new FormData();
    formData.append("file", file);

    setIsLoading(true);
    setError(null);
    setDatasetCategory("upload");

    try {
      const res = await fetch("http://localhost:3000/simulations/upload", {
        method: "POST",
        body: formData,
      });

      if (!res.ok) {
        throw new Error("Failed to parse CSV file");
      }

      const responseData = await res.json();
      if (responseData.resources && responseData.parcels) {
        setDepot({
          lat: responseData.resources.depot_location.lat,
          lng: responseData.resources.depot_location.lng
        });
        setDrivers(responseData.resources.num_drivers || 5);
        setMaxWeight(responseData.resources.max_weight_capacity || 20);
        setMaxVolume(responseData.resources.max_volume_capacity || 50);
        setParcels(responseData.parcels);
        setAllResults({});
        setActiveResult(null);
      }
    } catch (err: any) {
      setError(err.message || "An error occurred during file upload");
    } finally {
      setIsLoading(false);
    }
  };

  const handleLoadInstance = async (category: string, filename: string) => {
    if (!filename) return;
    setIsLoading(true);
    setError(null);
    try {
      const res = await fetch(`http://localhost:3000/instances/${category}/${filename}`);
      if (!res.ok) throw new Error("Failed to load instance file");
      const data = await res.json();
      if (data.resources && data.parcels) {
        setDepot({
          lat: data.resources.depot_location.lat,
          lng: data.resources.depot_location.lng
        });
        setDrivers(data.resources.num_drivers || 5);
        setMaxWeight(data.resources.max_weight_capacity || 20);
        setMaxVolume(data.resources.max_volume_capacity || 50);
        setParcels(data.parcels);
        setAllResults({});
        setActiveResult(null);
      }
    } catch (err: any) {
      setError(err.message || "An error occurred while loading dataset file");
    } finally {
      setIsLoading(false);
    }
  };

  const runSimulationForSolver = async (algoName: string) => {
    const backendSolver = algoName === "C++ Engine" ? "GA" : (algoName === "Baseline" ? "NN" : "ORTools");
    const payload = {
      solver: backendSolver,
      instanceJson: {
        resources: {
          depot_location: {
            lat: depot.lat,
            lng: depot.lng,
            osm_id: "depot"
          },
          num_drivers: drivers,
          max_weight_capacity: maxWeight,
          max_volume_capacity: maxVolume
        },
        parcels: parcels.map(p => ({
          id: p.id,
          lat: p.lat,
          lng: p.lng,
          weight: p.weight,
          volume: p.volume,
          time_window: p.time_window
        }))
      }
    };

    const res = await fetch("http://localhost:3000/simulations", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });

    if (!res.ok) {
      throw new Error(`Simulation failed for ${algoName}`);
    }

    const data = await res.json();
    return data.data;
  };

  const handleRunSimulation = async () => {
    setIsLoading(true);
    setError(null);

    try {
      const mainRes = await runSimulationForSolver(algorithm);
      setAllResults(prev => ({ ...prev, [algorithm]: mainRes }));
      setActiveResult(mainRes);

      const otherAlgos = ["C++ Engine", "Baseline", "OR-Tools"].filter(a => a !== algorithm);
      for (const algo of otherAlgos) {
        runSimulationForSolver(algo)
          .then(res => {
            setAllResults(prev => ({ ...prev, [algo]: res }));
          })
          .catch(err => console.error(err));
      }
    } catch (err: any) {
      setError(err.message || "An error occurred during simulation execution");
    } finally {
      setIsLoading(false);
    }
  };

  const getMappedRoutes = () => {
    if (!activeResult || !activeResult.routes) return [];
    return activeResult.routes.map((route: any) => {
      const sequence = route.sequence.map((node: any) => {
        if (node.location_id === 0) {
          return {
            ...node,
            lat: depot.lat,
            lng: depot.lng
          };
        } else {
          const parcel = parcels.find(p => p.id === node.location_id);
          return {
            ...node,
            lat: parcel ? parcel.lat : depot.lat,
            lng: parcel ? parcel.lng : depot.lng
          };
        }
      });
      return {
        ...route,
        sequence
      };
    });
  };

  const getDisplayValue = (field: string) => {
    if (!activeResult) return "-";
    if (field === "distance") {
      return `${(activeResult.total_distance).toFixed(2)} km`;
    }
    if (field === "baseline") {
      const baselineRes = allResults["Baseline"];
      if (baselineRes) {
        return `${(baselineRes.total_distance).toFixed(2)} km`;
      }
      return "-";
    }
    if (field === "time") {
      return `${activeResult.execution_time_ms.toFixed(1)} ms`;
    }
    if (field === "valid") {
      return activeResult.status === "SUCCESS" ? "100%" : "Incomplete";
    }
    return "-";
  };

  const getDriverRoutesMetrics = () => {
    if (!activeResult || !activeResult.routes) return [];
    return activeResult.routes.map((route: any) => {
      let weight = 0;
      let volume = 0;
      const parcelIds: number[] = route.parcel_ids || (route.sequence
        ? route.sequence
            .filter((node: any) => node.parcel_id !== null && node.parcel_id !== undefined)
            .map((node: any) => node.parcel_id)
        : []);
      parcelIds.forEach((pid: number) => {
        const parcel = parcels.find(p => p.id === pid);
        if (parcel) {
          weight += parcel.weight;
          volume += parcel.volume;
        }
      });
      return {
        vehicleId: route.vehicle_id,
        weight: weight.toFixed(1),
        volume: volume.toFixed(1),
        distance: `${Number(route.route_distance).toFixed(2)} km`,
        duration: `${((route.route_duration || 0) / 60).toFixed(1)} mins`
      };
    });
  };

  const getActiveDriverRouteSequence = () => {
    if (!activeResult || !activeResult.routes || activeResult.routes.length <= selectedDriverIndex) return [];
    const route = activeResult.routes[selectedDriverIndex];
    return route.sequence.map((node: any, idx: number) => {
      const isDepot = node.location_id === 0;
      const parcel = !isDepot ? parcels.find(p => p.id === node.location_id) : null;
      
      let timeWindowStr = "-";
      if (parcel) {
        const startH = Math.floor(parcel.time_window[0] / 60);
        const startM = parcel.time_window[0] % 60;
        const endH = Math.floor(parcel.time_window[1] / 60);
        const endM = parcel.time_window[1] % 60;
        timeWindowStr = `${startH}:${startM.toString().padStart(2, "0")} - ${endH}:${endM.toString().padStart(2, "0")}`;
      }

      const arrH = Math.floor(node.arrival_time / 60);
      const arrM = Math.floor(node.arrival_time % 60);
      const etaStr = `${arrH.toString().padStart(2, "0")}:${arrM.toString().padStart(2, "0")}`;

      return {
        seq: idx,
        pointId: isDepot ? "Depot" : `P${node.location_id.toString().padStart(3, "0")}`,
        timeWindow: timeWindowStr,
        eta: etaStr,
        status: isDepot ? "Start" : "On Time"
      };
    });
  };

  const getAnalyticsMetric = (algo: string, field: string) => {
    const res = allResults[algo];
    if (!res) return "-";
    if (field === "time") {
      return `${res.execution_time_ms.toFixed(1)} ms`;
    }
    if (field === "distance") {
      return `${(res.total_distance).toFixed(2)} km`;
    }
    return "-";
  };

  const driverMetrics = getDriverRoutesMetrics();

  return (
    <div className="flex h-screen bg-gray-50 text-gray-900 font-sans overflow-hidden">
      <div className="w-[340px] bg-white border-r border-gray-200 flex flex-col h-full shrink-0">
        <div className="p-5 border-b border-gray-200">
          <h1 className="text-xl font-bold tracking-tight text-gray-900">Motorcycle Routing Module</h1>
        </div>

        <div className="flex-1 overflow-y-auto p-5 space-y-6">
          <input 
            type="file" 
            ref={fileInputRef} 
            onChange={handleFileChange} 
            accept=".csv" 
            className="hidden" 
          />

          <div 
            onClick={handleUploadClick}
            className="border-2 border-dashed border-gray-300 rounded-xl p-6 flex flex-col items-center justify-center text-gray-500 hover:bg-gray-50 cursor-pointer transition-colors"
          >
            <Upload className="w-8 h-8 mb-2 text-gray-400" />
            <span className="font-medium text-sm">Upload Parcel CSV</span>
            {parcels.length > 0 && datasetCategory === "upload" && (
              <span className="text-xs text-green-600 mt-1 font-semibold">Loaded {parcels.length} parcels</span>
            )}
          </div>

          <div className="bg-white border border-gray-200 rounded-xl p-5">
            <h2 className="text-sm font-semibold text-gray-900 mb-4 text-center">Fleet & Constraints</h2>
            
            <div className="mb-4">
              <label className="block text-xs text-gray-500 mb-1 text-center">Number of Available Drivers</label>
              <input 
                type="number" 
                value={drivers}
                onChange={(e) => setDrivers(Number(e.target.value))}
                className="w-full border border-gray-300 rounded-lg p-2 text-center text-sm font-medium focus:ring-2 focus:ring-blue-500 focus:outline-none"
              />
            </div>

            <div className="grid grid-cols-2 gap-3">
              <div>
                <label className="block text-xs text-gray-500 mb-1 text-center">Max Weight (kg)</label>
                <input 
                  type="number" 
                  value={maxWeight}
                  onChange={(e) => setMaxWeight(Number(e.target.value))}
                  className="w-full border border-gray-300 rounded-lg p-2 text-center text-sm font-medium focus:ring-2 focus:ring-blue-500 focus:outline-none"
                />
              </div>
              <div>
                <label className="block text-xs text-gray-500 mb-1 text-center">Max Volume (L)</label>
                <input 
                  type="number" 
                  value={maxVolume}
                  onChange={(e) => setMaxVolume(Number(e.target.value))}
                  className="w-full border border-gray-300 rounded-lg p-2 text-center text-sm font-medium focus:ring-2 focus:ring-blue-500 focus:outline-none"
                />
              </div>
            </div>
          </div>

          <div className="bg-white border border-gray-200 rounded-xl p-5">
            <h2 className="text-sm font-semibold text-gray-900 mb-4 text-center">Simulation Settings</h2>
            
            <div className="flex bg-gray-100 p-1 rounded-lg mb-4">
              <button 
                onClick={() => setIsBatchMode(false)}
                className={`flex-1 py-1.5 text-xs font-semibold rounded-md transition-all ${!isBatchMode ? 'bg-white text-blue-600 shadow-sm' : 'text-gray-500 hover:text-gray-900'}`}
              >
                Single Instance
              </button>
              <button 
                onClick={() => setIsBatchMode(true)}
                className={`flex-1 py-1.5 text-xs font-semibold rounded-md transition-all ${isBatchMode ? 'bg-white text-blue-600 shadow-sm' : 'text-gray-500 hover:text-gray-900'}`}
              >
                Batch Simulation
              </button>
            </div>

            {!isBatchMode ? (
              <>
                <div className="mb-4">
                  <label className="block text-xs text-gray-500 mb-1">Dataset Category</label>
                  <select 
                    value={datasetCategory}
                    onChange={(e) => {
                      const cat = e.target.value;
                      setDatasetCategory(cat);
                      if (cat !== "upload") {
                        const list = availableInstances[cat] || [];
                        if (list.length > 0) {
                          setSelectedInstanceFilename(list[0]);
                          handleLoadInstance(cat, list[0]);
                        }
                      }
                    }}
                    className="w-full border border-gray-300 rounded-lg p-2 text-sm focus:ring-2 focus:ring-blue-500 focus:outline-none appearance-none bg-white"
                  >
                    <option value="upload">Uploaded CSV File</option>
                    <option value="scale">Scale Test (Size N)</option>
                    <option value="split_capability">Split Capability</option>
                    <option value="tightness">Tightness Constraints</option>
                  </select>
                </div>

                {datasetCategory !== "upload" && (
                  <div className="mb-4">
                    <label className="block text-xs text-gray-500 mb-1">Select Instance File</label>
                    <select 
                      value={selectedInstanceFilename}
                      onChange={(e) => {
                        const filename = e.target.value;
                        setSelectedInstanceFilename(filename);
                        handleLoadInstance(datasetCategory, filename);
                      }}
                      className="w-full border border-gray-300 rounded-lg p-2 text-sm focus:ring-2 focus:ring-blue-500 focus:outline-none appearance-none bg-white"
                    >
                      {(availableInstances[datasetCategory] || []).map((file) => (
                        <option key={file} value={file}>{file}</option>
                      ))}
                    </select>
                  </div>
                )}

                <div className="mb-6">
                  <label className="block text-xs text-gray-500 mb-1">Algorithm</label>
                  <div className="flex rounded-lg overflow-hidden border border-gray-300">
                    {["C++ Engine", "Baseline", "OR-Tools"].map(algo => (
                      <button 
                        key={algo}
                        onClick={() => {
                          setAlgorithm(algo);
                          if (allResults[algo]) {
                            setActiveResult(allResults[algo]);
                          }
                        }}
                        className={`flex-1 py-2 text-xs font-medium border-r last:border-r-0 border-gray-300 transition-colors
                          ${algorithm === algo ? 'bg-blue-100 text-blue-700' : 'bg-gray-50 text-gray-600 hover:bg-gray-100'}`}
                      >
                        {algo}
                      </button>
                    ))}
                  </div>
                </div>

                <button 
                  onClick={handleRunSimulation}
                  disabled={isLoading}
                  className="w-full bg-blue-600 hover:bg-blue-700 text-white font-medium py-3 rounded-lg text-sm transition-colors shadow-sm disabled:bg-gray-400"
                >
                  {isLoading ? "Running..." : "Run Simulation"}
                </button>
              </>
            ) : (
              <>
                <div className="bg-blue-50 border border-blue-200 rounded-xl p-4 text-xs text-blue-800 leading-relaxed mb-4">
                  Run solver recursively across the complete fleet benchmark dataset of 300 instances (Scale, Split Capability, and Tightness). Tracks execution speeds, optimization gaps, and vehicle utilization.
                </div>
                
                <button 
                  onClick={startBatchSimulation}
                  disabled={batchRunning}
                  className="w-full bg-blue-600 hover:bg-blue-700 text-white font-semibold py-3 rounded-lg text-sm transition-all shadow-md disabled:bg-gray-400"
                >
                  {batchRunning ? "Executing Batch..." : "Run Full Fleet Benchmark"}
                </button>
              </>
            )}

            {error && (
              <p className="text-xs text-red-600 mt-2 font-medium text-center">{error}</p>
            )}
          </div>

          <div>
            <h2 className="text-sm font-bold text-gray-900 mb-3">Simulation Results</h2>
            <div className="grid grid-cols-2 gap-3">
              <div className="bg-blue-50/50 border-l-4 border-blue-500 rounded-r-lg p-3">
                <p className="text-xs text-gray-500 font-medium">Total Distance</p>
                <p className="text-xl font-bold text-blue-600 mt-1">{getDisplayValue("distance")}</p>
              </div>
              <div className="bg-gray-50 border-l-4 border-gray-400 rounded-r-lg p-3">
                <p className="text-xs text-gray-500 font-medium">Baseline (NN)</p>
                <p className="text-xl font-bold text-gray-700 mt-1">{getDisplayValue("baseline")}</p>
              </div>
              <div className="bg-orange-50/50 border-l-4 border-orange-500 rounded-r-lg p-3">
                <p className="text-xs text-gray-500 font-medium">Execution Time</p>
                <p className="text-xl font-bold text-orange-600 mt-1">{getDisplayValue("time")}</p>
              </div>
              <div className="bg-green-50/50 border-l-4 border-green-500 rounded-r-lg p-3">
                <p className="text-xs text-gray-500 font-medium">Time Window Valid</p>
                <p className="text-xl font-bold text-green-600 mt-1">{getDisplayValue("valid")}</p>
              </div>
            </div>
          </div>
        </div>
      </div>

      <div className="flex-1 flex flex-col min-w-0 bg-white">
        <div className="h-16 border-b border-gray-200 px-8 flex items-end">
          <div className="flex gap-8">
            {tabs.map(tab => (
              <button
                key={tab}
                onClick={() => setActiveTab(tab)}
                className={`pb-4 text-sm font-semibold transition-colors border-b-2 
                  ${activeTab === tab ? 'border-blue-600 text-blue-600' : 'border-transparent text-gray-500 hover:text-gray-900'}`}
              >
                {tab}
              </button>
            ))}
          </div>
        </div>

        <div className="flex-1 p-6 overflow-hidden bg-gray-50/50">
          {activeTab === "Simulation Map" && (
             <MapView 
               routes={getMappedRoutes()} 
               showZone={showZone} 
               setShowZone={setShowZone}
               parcels={parcels} 
               depot={depot} 
             />
          )}

          {activeTab === "Fleet Schedule" && (
             <div className="h-full overflow-y-auto space-y-6">
               <div className="bg-white border border-gray-200 rounded-xl p-6 shadow-sm">
                 <h3 className="text-sm font-bold text-gray-900 mb-4">Route Metrics per Driver</h3>
                 <div className="overflow-x-auto">
                   <table className="w-full text-sm text-left">
                     <thead className="bg-[#0f172a] text-white">
                       <tr>
                         <th className="px-6 py-3 font-medium rounded-tl-lg">Driver</th>
                         <th className="px-6 py-3 font-medium text-center">Total Weight (kg)</th>
                         <th className="px-6 py-3 font-medium text-center">Total Volume (L)</th>
                         <th className="px-6 py-3 font-medium text-center">Total Distance</th>
                         <th className="px-6 py-3 font-medium text-center rounded-tr-lg">Total Travel Time</th>
                       </tr>
                     </thead>
                      <tbody className="divide-y divide-gray-100">
                        {driverMetrics.slice(driverMetricsPage * ITEMS_PER_PAGE, (driverMetricsPage + 1) * ITEMS_PER_PAGE).map((m: any, index: number) => {
                          const actualIndex = driverMetricsPage * ITEMS_PER_PAGE + index;
                          return (
                            <tr 
                              key={actualIndex}
                              onClick={() => { setSelectedDriverIndex(actualIndex); setSequencePage(0); }}
                              className={`hover:bg-gray-50 cursor-pointer ${selectedDriverIndex === actualIndex ? "bg-blue-50/70" : ""}`}
                            >
                              <td className="px-6 py-4 font-medium">Driver {actualIndex + 1}</td>
                              <td className="px-6 py-4 text-center">{m.weight} / {maxWeight}</td>
                              <td className="px-6 py-4 text-center">{m.volume} / {maxVolume}</td>
                              <td className="px-6 py-4 text-center">{m.distance}</td>
                              <td className="px-6 py-4 text-center">{m.duration}</td>
                            </tr>
                          );
                        })}
                        {driverMetrics.length === 0 && (
                          <tr>
                            <td colSpan={5} className="text-center py-6 text-gray-500 font-medium">
                              No simulation data available. Please click &quot;Run Simulation&quot; first.
                            </td>
                          </tr>
                        )}
                      </tbody>
                    </table>
                  </div>
                  {driverMetrics.length > ITEMS_PER_PAGE && (
                    <div className="flex justify-between items-center mt-4 pt-4 border-t border-gray-100">
                      <span className="text-xs text-gray-500 font-medium">
                        Showing {driverMetricsPage * ITEMS_PER_PAGE + 1} to {Math.min((driverMetricsPage + 1) * ITEMS_PER_PAGE, driverMetrics.length)} of {driverMetrics.length} drivers
                      </span>
                      <div className="flex gap-2">
                        <button
                          disabled={driverMetricsPage === 0}
                          onClick={() => setDriverMetricsPage(prev => prev - 1)}
                          className="px-3 py-1.5 rounded-lg border border-gray-300 text-xs font-semibold bg-white text-gray-700 hover:bg-gray-50 disabled:bg-gray-100 disabled:text-gray-400 transition-colors cursor-pointer"
                        >
                          Previous
                        </button>
                        <button
                          disabled={driverMetricsPage === Math.ceil(driverMetrics.length / ITEMS_PER_PAGE) - 1}
                          onClick={() => setDriverMetricsPage(prev => prev + 1)}
                          className="px-3 py-1.5 rounded-lg border border-gray-300 text-xs font-semibold bg-white text-gray-700 hover:bg-gray-50 disabled:bg-gray-100 disabled:text-gray-400 transition-colors cursor-pointer"
                        >
                          Next
                        </button>
                      </div>
                    </div>
                  )}
                </div>

                {driverMetrics.length > 0 && (
                  <div className="bg-white border border-gray-200 rounded-xl p-6 shadow-sm">
                    <div className="flex justify-between items-center mb-4">
                      <h3 className="text-sm font-bold text-gray-900">
                        Detailed Sequence - Driver {selectedDriverIndex + 1}
                      </h3>
                      <div className="flex items-center gap-2">
                        <span className="text-xs text-gray-500">Select Driver:</span>
                        <select
                          value={selectedDriverIndex}
                          onChange={(e) => { setSelectedDriverIndex(Number(e.target.value)); setSequencePage(0); }}
                          className="border border-gray-300 rounded-lg p-1 text-xs focus:ring-2 focus:ring-blue-500 focus:outline-none bg-white font-medium"
                        >
                          {driverMetrics.map((m: any, index: number) => (
                            <option key={index} value={index}>
                              Driver {index + 1}
                            </option>
                          ))}
                        </select>
                      </div>
                    </div>
                   <div className="overflow-x-auto">
                     <table className="w-full text-sm text-center">
                       <thead className="bg-[#0f172a] text-white">
                         <tr>
                           <th className="px-6 py-3 font-medium rounded-tl-lg">Sequence</th>
                           <th className="px-6 py-3 font-medium">Delivery Point (ID)</th>
                           <th className="px-6 py-3 font-medium">Time Window</th>
                           <th className="px-6 py-3 font-medium">ETA</th>
                           <th className="px-6 py-3 font-medium rounded-tr-lg">Status</th>
                         </tr>
                       </thead>
                       <tbody className="divide-y divide-gray-100">
                          {getActiveDriverRouteSequence().slice(sequencePage * ITEMS_PER_PAGE, (sequencePage + 1) * ITEMS_PER_PAGE).map((s: any, idx: number) => (
                            <tr key={idx} className="hover:bg-gray-50">
                              <td className="px-6 py-4">{s.seq}</td>
                              <td className="px-6 py-4 font-medium">{s.pointId}</td>
                              <td className="px-6 py-4">{s.timeWindow}</td>
                              <td className="px-6 py-4">{s.eta}</td>
                              <td className="px-6 py-4 text-green-600 font-medium">{s.status}</td>
                            </tr>
                          ))}
                        </tbody>
                      </table>
                    </div>
                    {getActiveDriverRouteSequence().length > ITEMS_PER_PAGE && (
                      <div className="flex justify-between items-center mt-4 pt-4 border-t border-gray-100">
                        <span className="text-xs text-gray-500 font-medium">
                          Showing {sequencePage * ITEMS_PER_PAGE + 1} to {Math.min((sequencePage + 1) * ITEMS_PER_PAGE, getActiveDriverRouteSequence().length)} of {getActiveDriverRouteSequence().length} stops
                        </span>
                        <div className="flex gap-2">
                          <button
                            disabled={sequencePage === 0}
                            onClick={() => setSequencePage(prev => prev - 1)}
                            className="px-3 py-1.5 rounded-lg border border-gray-300 text-xs font-semibold bg-white text-gray-700 hover:bg-gray-50 disabled:bg-gray-100 disabled:text-gray-400 transition-colors cursor-pointer"
                          >
                            Previous
                          </button>
                          <button
                            disabled={sequencePage === Math.ceil(getActiveDriverRouteSequence().length / ITEMS_PER_PAGE) - 1}
                            onClick={() => setSequencePage(prev => prev + 1)}
                            className="px-3 py-1.5 rounded-lg border border-gray-300 text-xs font-semibold bg-white text-gray-700 hover:bg-gray-50 disabled:bg-gray-100 disabled:text-gray-400 transition-colors cursor-pointer"
                          >
                            Next
                          </button>
                        </div>
                      </div>
                    )}
                 </div>
               )}
             </div>
          )}

          {activeTab === "Analytics" && (
             <div className="h-full overflow-y-auto space-y-8 pr-1">
               {benchmarkData.length === 0 ? (
                 <div className="bg-white border border-gray-200 rounded-3xl p-12 shadow-sm flex flex-col items-center justify-center text-center max-w-2xl mx-auto my-12">
                   <div className="w-20 h-20 bg-blue-50 rounded-2xl flex items-center justify-center mb-6 text-blue-600">
                     <svg className="w-10 h-10" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                       <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z" />
                     </svg>
                   </div>
                   <h3 className="text-xl font-bold text-gray-900 mb-2">No Benchmarking Analytics Available</h3>
                   <p className="text-sm text-gray-500 max-w-md leading-relaxed mb-8">
                     To view interactive comparison charts, solver convergence gaps, and performance metrics, please initiate a full batch simulation across the 300 instances.
                   </p>
                   <button
                     onClick={startBatchSimulation}
                     className="bg-blue-600 hover:bg-blue-700 text-white font-semibold px-8 py-3.5 rounded-xl text-sm transition-all shadow-md cursor-pointer"
                   >
                     Trigger Full Fleet Benchmark
                   </button>
                 </div>
               ) : (() => {
                 const getCategory = (name: string) => {
                   if (name.startsWith("scale")) return "Scale";
                   if (name.startsWith("split")) return "Split Capability";
                   if (name.startsWith("tight")) return "Tightness";
                   return "Other";
                 };

                  const getRawDist = (cost: number, undelivered: number) => {
                    const rawDist = Math.max(0, cost - (undelivered * 10000000.0)) / 1000.0;
                    return rawDist + (undelivered * 100.0);
                  };

                 const filtered = benchmarkData.filter(d => {
                   if (analyticsCategoryFilter === "All") return true;
                   return getCategory(d.Instance) === analyticsCategoryFilter;
                 });

                  const total = filtered.length || 1;
                  const feasibleForNn = filtered.filter(d => d.NN_Undelivered === 0);
                  const avgNn = feasibleForNn.length > 0
                    ? feasibleForNn.reduce((acc, d) => acc + getRawDist(d.NN_Cost, 0), 0) / feasibleForNn.length
                    : 0;
                  const feasibleForGa = filtered.filter(d => d.GA_Undelivered === 0);
                  const avgGa = feasibleForGa.length > 0
                    ? feasibleForGa.reduce((acc, d) => acc + getRawDist(d.GA_Cost, 0), 0) / feasibleForGa.length
                    : 0;
                  const feasibleForOrt = filtered.filter(d => d.ORT_Undelivered === 0);
                  const avgOrt = feasibleForOrt.length > 0
                    ? feasibleForOrt.reduce((acc, d) => acc + getRawDist(d.ORT_Cost, 0), 0) / feasibleForOrt.length
                    : 0;

                 const gaImprovement = filtered.reduce((acc, d) => {
                   const nnDist = getRawDist(d.NN_Cost, d.NN_Undelivered);
                   const gaDist = getRawDist(d.GA_Cost, d.GA_Undelivered);
                   if (nnDist === 0) return acc;
                   return acc + ((nnDist - gaDist) / nnDist * 100);
                 }, 0) / total;

                 const ortImprovement = filtered.reduce((acc, d) => {
                   const nnDist = getRawDist(d.NN_Cost, d.NN_Undelivered);
                   const ortDist = getRawDist(d.ORT_Cost, d.ORT_Undelivered);
                   if (nnDist === 0) return acc;
                   return acc + ((nnDist - ortDist) / nnDist * 100);
                 }, 0) / total;

                 const gaTime = filtered.reduce((acc, d) => acc + d.GA_ms, 0) / total;
                 const ortTime = filtered.reduce((acc, d) => acc + d.ORT_ms, 0) / total;
                 const speedup = ortTime / Math.max(0.1, gaTime);

                 const scaleData = benchmarkData.filter(d => getCategory(d.Instance) === "Scale");
                 const splitData = benchmarkData.filter(d => getCategory(d.Instance) === "Split Capability");
                 const tightData = benchmarkData.filter(d => getCategory(d.Instance) === "Tightness");

                 const calcCatMetrics = (data: any[]) => {
                   const count = data.length || 1;
                   
                   const getParcelCount = (name: string) => {
                     const m = name.match(/_(\d+)(?:_|\.json)/);
                     return m ? parseInt(m[1]) : 50;
                   };

                    const nnDeliveryRate = data.reduce((acc, d) => {
                      const total = getParcelCount(d.Instance);
                      return acc + ((total - d.NN_Undelivered) / total * 100);
                    }, 0) / count;

                    const gaDeliveryRate = data.reduce((acc, d) => {
                      const total = getParcelCount(d.Instance);
                      return acc + ((total - d.GA_Undelivered) / total * 100);
                    }, 0) / count;

                    const ortDeliveryRate = data.reduce((acc, d) => {
                      const total = getParcelCount(d.Instance);
                      return acc + ((total - d.ORT_Undelivered) / total * 100);
                    }, 0) / count;

                    const feasibleForNn = data.filter(d => d.NN_Undelivered === 0);
                    const nnDist = feasibleForNn.length > 0
                      ? feasibleForNn.reduce((acc, d) => acc + getRawDist(d.NN_Cost, 0), 0) / feasibleForNn.length
                      : 0;
                    const feasibleForGa = data.filter(d => d.GA_Undelivered === 0);
                    const gaDist = feasibleForGa.length > 0
                      ? feasibleForGa.reduce((acc, d) => acc + getRawDist(d.GA_Cost, 0), 0) / feasibleForGa.length
                      : 0;
                    const feasibleForOrt = data.filter(d => d.ORT_Undelivered === 0);
                    const ortDist = feasibleForOrt.length > 0
                      ? feasibleForOrt.reduce((acc, d) => acc + getRawDist(d.ORT_Cost, 0), 0) / feasibleForOrt.length
                      : 0;

                   const nnTime = data.reduce((acc, d) => acc + d.NN_ms, 0) / count;
                   const gaTime = data.reduce((acc, d) => acc + d.GA_ms, 0) / count;
                   const ortTime = data.reduce((acc, d) => acc + d.ORT_ms, 0) / count;

                   const gaSaved = data.reduce((acc, d) => {
                     const nnD = getRawDist(d.NN_Cost, d.NN_Undelivered);
                     const gaD = getRawDist(d.GA_Cost, d.GA_Undelivered);
                     return nnD === 0 ? acc : acc + ((nnD - gaD) / nnD * 100);
                   }, 0) / count;

                   const ortSaved = data.reduce((acc, d) => {
                     const nnD = getRawDist(d.NN_Cost, d.NN_Undelivered);
                     const ortD = getRawDist(d.ORT_Cost, d.ORT_Undelivered);
                     return nnD === 0 ? acc : acc + ((nnD - ortD) / nnD * 100);
                   }, 0) / count;

                    const nnSolved = data.filter(d => d.NN_Undelivered === 0).length;
                    const gaSolved = data.filter(d => d.GA_Undelivered === 0).length;
                    const ortSolved = data.filter(d => d.ORT_Undelivered === 0).length;

                   return {
                     count,
                     nnDist, gaDist, ortDist,
                     nnTime, gaTime, ortTime,
                     nnDeliveryRate, gaDeliveryRate, ortDeliveryRate,
                     gaSaved, ortSaved,
                     nnSolved, gaSolved, ortSolved
                   };
                 };

                 const overallSum = calcCatMetrics(benchmarkData);
                 const scaleSum = calcCatMetrics(scaleData);
                 const splitSum = calcCatMetrics(splitData);
                 const tightSum = calcCatMetrics(tightData);

                 const getCatImp = (data: any[]) => {
                   if (data.length === 0) return 0;
                   return data.reduce((acc, d) => {
                     const nnDist = getRawDist(d.NN_Cost, d.NN_Undelivered);
                     const gaDist = getRawDist(d.GA_Cost, d.GA_Undelivered);
                     if (nnDist === 0) return acc;
                     return acc + ((nnDist - gaDist) / nnDist * 100);
                   }, 0) / data.length;
                 };

                 const sizes = [50, 100, 200, 500];
                 const lineChartData = sizes.map(sz => {
                   const insts = benchmarkData.filter(d => {
                     const s = parseInt(d.Instance.match(/_(\d+)(?:_|\.json)/)?.[1] || "0");
                     return s === sz && getCategory(d.Instance) === "Scale";
                   });
                   const count = insts.length || 1;
                   return {
                     size: sz,
                     ga: insts.reduce((acc, d) => acc + d.GA_ms, 0) / count,
                     ort: insts.reduce((acc, d) => acc + d.ORT_ms, 0) / count,
                     nn: insts.reduce((acc, d) => acc + d.NN_ms, 0) / count
                   };
                 });

                 const maxTimeVal = Math.max(...lineChartData.flatMap(d => [d.ga, d.ort, d.nn]), 1);
                 const getLineY = (val: number) => 180 - (val / maxTimeVal * 140);

                 const maxCostVal = Math.max(avgNn, avgGa, avgOrt, 1);
                 const getBarHeight = (val: number) => (val / maxCostVal * 130);

                 const categories = ["All", "Scale", "Split Capability", "Tightness"];
                 const pageCount = Math.ceil(filtered.length / 10);
                 const displayRows = filtered.slice(masterTablePage * 10, (masterTablePage + 1) * 10);

                 return (
                   <>
                     <div className="flex justify-between items-center bg-white border border-gray-200 rounded-2xl p-6 shadow-sm">
                       <div>
                         <h2 className="text-lg font-black text-gray-900">Fleet Benchmarking Dashboard</h2>
                         <p className="text-xs text-gray-500 mt-1">Comparative metrics for C++ GASolver, Nearest Neighbor baseline, and Google OR-Tools</p>
                       </div>
                       <div className="flex gap-3">
                         <button
                           onClick={exportCSV}
                           className="bg-white hover:bg-gray-50 text-gray-700 font-semibold border border-gray-300 px-4 py-2.5 rounded-xl text-xs flex items-center gap-2 cursor-pointer transition-colors shadow-sm"
                         >
                           Export Raw CSV
                         </button>
                       </div>
                     </div>

                      <div className="bg-white border border-gray-200 rounded-2xl p-6 shadow-sm space-y-4">
                        <div className="border-b border-gray-100 pb-3">
                          <h3 className="text-sm font-bold text-gray-900">Fleet Benchmarking Executive Summary Table</h3>
                          <p className="text-[10px] text-gray-500 mt-0.5">Comprehensive multi-dimensional comparison of Nearest Neighbor (NN), C++ Genetic Algorithm (GA), and Google OR-Tools solvers.</p>
                        </div>
                        <div className="overflow-x-auto">
                          <table className="w-full text-center border-collapse text-xs">
                            <thead>
                              <tr className="bg-[#1e293b] text-white">
                                <th rowSpan={2} className="px-3 py-3 font-semibold rounded-tl-lg text-left border-r border-slate-700">Category</th>
                                <th colSpan={4} className="px-3 py-1.5 font-semibold text-center border-r border-slate-700">Baseline (Nearest Neighbor)</th>
                                <th colSpan={5} className="px-3 py-1.5 font-semibold text-center border-r border-slate-700">Proposed C++ (Genetic Algorithm)</th>
                                <th colSpan={5} className="px-3 py-1.5 font-semibold text-center rounded-tr-lg">Google OR-Tools Solver</th>
                              </tr>
                              <tr className="bg-[#334155] text-slate-100 text-[10px]">
                                <th className="px-2 py-1.5 font-semibold">Avg Dist</th>
                                <th className="px-2 py-1.5 font-semibold">Runtime</th>
                                <th className="px-2 py-1.5 font-semibold">Delivery Rate</th>
                                <th className="px-2 py-1.5 font-semibold border-r border-slate-700">Solved</th>
                                
                                <th className="px-2 py-1.5 font-semibold">Avg Dist</th>
                                <th className="px-2 py-1.5 font-semibold">Runtime</th>
                                <th className="px-2 py-1.5 font-semibold">Delivery Rate</th>
                                <th className="px-2 py-1.5 font-semibold">Solved</th>
                                <th className="px-2 py-1.5 font-semibold border-r border-slate-700">Distance Saved</th>
                                
                                <th className="px-2 py-1.5 font-semibold">Avg Dist</th>
                                <th className="px-2 py-1.5 font-semibold">Runtime</th>
                                <th className="px-2 py-1.5 font-semibold">Delivery Rate</th>
                                <th className="px-2 py-1.5 font-semibold">Solved</th>
                                <th className="px-2 py-1.5 font-semibold">Distance Saved</th>
                              </tr>
                            </thead>
                            <tbody className="divide-y divide-gray-100 font-medium text-gray-700">
                              {[
                                { name: "Overall Dataset", sum: overallSum, isSplit: false },
                                { name: "Scale Instances", sum: scaleSum, isSplit: false },
                                { name: "Split Capability", sum: splitSum, isSplit: true },
                                { name: "Tightness Constraints", sum: tightSum, isSplit: false }
                              ].map((row, idx) => {
                                const formatSaved = (pct: number) => {
                                  return (
                                    <span className={`font-bold ${pct >= 0 ? 'text-green-600' : 'text-red-500'}`}>
                                      {pct >= 0 ? "+" : ""}{pct.toFixed(1)}%
                                    </span>
                                  );
                                };
                                return (
                                  <tr key={idx} className="hover:bg-gray-50/50 transition-colors">
                                    <td className="px-3 py-3.5 text-left font-bold text-gray-800 border-r border-gray-100">{row.name}</td>
                                    
                                    <td className="px-2 py-3.5">{row.sum.nnDist.toFixed(1)} km</td>
                                    <td className="px-2 py-3.5">{row.sum.nnTime.toFixed(0)} ms</td>
                                    <td className="px-2 py-3.5">{row.sum.nnDeliveryRate.toFixed(1)}%</td>
                                    <td className="px-2 py-3.5 border-r border-gray-100 font-bold">{row.sum.nnSolved} / {row.sum.count}</td>
                                    
                                    <td className="px-2 py-3.5 font-bold text-gray-900">{row.sum.gaDist.toFixed(1)} km</td>
                                    <td className="px-2 py-3.5">{row.sum.gaTime.toFixed(0)} ms</td>
                                    <td className="px-2 py-3.5 font-bold text-gray-900">{row.sum.gaDeliveryRate.toFixed(1)}%</td>
                                    <td className="px-2 py-3.5 font-bold text-gray-900">{row.sum.gaSolved} / {row.sum.count}</td>
                                    <td className="px-2 py-3.5 border-r border-gray-100 bg-gray-50/30">{formatSaved(row.sum.gaSaved)}</td>
                                    
                                    {row.isSplit ? (
                                      <>
                                        <td colSpan={5} className="px-2 py-3.5 text-red-500 font-bold bg-red-50/20 text-center tracking-wide">
                                          N/A (Google OR-Tools does not support Split VRP)
                                        </td>
                                      </>
                                    ) : (
                                      <>
                                        <td className="px-2 py-3.5 text-gray-800 font-bold">{row.sum.ortDist.toFixed(1)} km</td>
                                        <td className="px-2 py-3.5">{row.sum.ortTime.toFixed(0)} ms</td>
                                        <td className="px-2 py-3.5 font-bold">{row.sum.ortDeliveryRate.toFixed(1)}%</td>
                                        <td className="px-2 py-3.5 font-bold">{row.sum.ortSolved} / {row.sum.count}</td>
                                        <td className="px-2 py-3.5 bg-gray-50/30">{formatSaved(row.sum.ortSaved)}</td>
                                      </>
                                    )}
                                  </tr>
                                );
                              })}
                            </tbody>
                          </table>
                        </div>
                      </div>

                     <div className="grid grid-cols-2 gap-6">
                       <div className="bg-white border border-gray-200 rounded-2xl p-6 shadow-sm">
                         <h3 className="text-sm font-bold text-gray-900 mb-6">Runtime Scaling: Time vs Size (Scale Category)</h3>
                         <div className="w-full h-[220px]">
                           <svg viewBox="0 0 500 220" className="w-full h-full">
                             <line x1="50" y1="30" x2="450" y2="30" stroke="#f1f5f9" strokeWidth="1" strokeDasharray="4 4" />
                             <line x1="50" y1="80" x2="450" y2="80" stroke="#f1f5f9" strokeWidth="1" strokeDasharray="4 4" />
                             <line x1="50" y1="130" x2="450" y2="130" stroke="#f1f5f9" strokeWidth="1" strokeDasharray="4 4" />
                             <line x1="50" y1="180" x2="450" y2="180" stroke="#cbd5e1" strokeWidth="1" />

                             <line x1="50" y1="30" x2="50" y2="180" stroke="#f1f5f9" strokeWidth="1" />
                             <line x1="183" y1="30" x2="183" y2="180" stroke="#f1f5f9" strokeWidth="1" />
                             <line x1="316" y1="30" x2="316" y2="180" stroke="#f1f5f9" strokeWidth="1" />
                             <line x1="450" y1="30" x2="450" y2="180" stroke="#f1f5f9" strokeWidth="1" />

                             <path
                               d={`M 50 ${getLineY(lineChartData[0].ga)} L 183 ${getLineY(lineChartData[1].ga)} L 316 ${getLineY(lineChartData[2].ga)} L 450 ${getLineY(lineChartData[3].ga)}`}
                               fill="none"
                               stroke="#2563eb"
                               strokeWidth="3"
                               strokeLinecap="round"
                             />
                             <path
                               d={`M 50 ${getLineY(lineChartData[0].ort)} L 183 ${getLineY(lineChartData[1].ort)} L 316 ${getLineY(lineChartData[2].ort)} L 450 ${getLineY(lineChartData[3].ort)}`}
                               fill="none"
                               stroke="#d97706"
                               strokeWidth="3"
                               strokeLinecap="round"
                             />
                             <path
                               d={`M 50 ${getLineY(lineChartData[0].nn)} L 183 ${getLineY(lineChartData[1].nn)} L 316 ${getLineY(lineChartData[2].nn)} L 450 ${getLineY(lineChartData[3].nn)}`}
                               fill="none"
                               stroke="#64748b"
                               strokeWidth="2"
                               strokeLinecap="round"
                               strokeDasharray="3 3"
                             />

                             {lineChartData.map((pt, idx) => {
                               const x = 50 + idx * 133;
                               return (
                                 <g key={idx}>
                                   <circle cx={x} cy={getLineY(pt.ga)} r="4" fill="#2563eb" stroke="#ffffff" strokeWidth="1.5" />
                                   <circle cx={x} cy={getLineY(pt.ort)} r="4" fill="#d97706" stroke="#ffffff" strokeWidth="1.5" />
                                   <circle cx={x} cy={getLineY(pt.nn)} r="4" fill="#64748b" stroke="#ffffff" strokeWidth="1.5" />
                                 </g>
                               );
                             })}

                             <text x="50" y="200" textAnchor="middle" className="text-[10px] fill-gray-500 font-semibold">N=50</text>
                             <text x="183" y="200" textAnchor="middle" className="text-[10px] fill-gray-500 font-semibold">N=100</text>
                             <text x="316" y="200" textAnchor="middle" className="text-[10px] fill-gray-500 font-semibold">N=200</text>
                             <text x="450" y="200" textAnchor="middle" className="text-[10px] fill-gray-500 font-semibold">N=500</text>

                             <text x="45" y="30" textAnchor="end" className="text-[9px] fill-gray-400 font-semibold">{(maxTimeVal).toFixed(0)}ms</text>
                             <text x="45" y="105" textAnchor="end" className="text-[9px] fill-gray-400 font-semibold">{(maxTimeVal / 2).toFixed(0)}ms</text>
                             <text x="45" y="180" textAnchor="end" className="text-[9px] fill-gray-400 font-semibold">0ms</text>
                           </svg>
                         </div>
                         <div className="flex justify-center gap-6 mt-4">
                           <div className="flex items-center gap-2">
                             <span className="w-3 h-3 rounded-full bg-blue-600" />
                             <span className="text-[10px] text-gray-600 font-semibold">Proposed C++</span>
                           </div>
                           <div className="flex items-center gap-2">
                             <span className="w-3 h-3 rounded-full bg-amber-600" />
                             <span className="text-[10px] text-gray-600 font-semibold">Google OR-Tools</span>
                           </div>
                           <div className="flex items-center gap-2">
                             <span className="w-3 h-3 bg-gray-500 border border-dashed border-gray-400" style={{ height: "4px" }} />
                             <span className="text-[10px] text-gray-600 font-semibold">Nearest Neighbor (Baseline)</span>
                           </div>
                         </div>
                       </div>

                       <div className="bg-white border border-gray-200 rounded-2xl p-6 shadow-sm">
                         <h3 className="text-sm font-bold text-gray-900 mb-6">Solution Quality: Avg Distance (km)</h3>
                         <div className="w-full h-[220px]">
                           <svg viewBox="0 0 400 220" className="w-full h-full">
                             <line x1="50" y1="30" x2="350" y2="30" stroke="#f1f5f9" strokeWidth="1" strokeDasharray="4 4" />
                             <line x1="50" y1="105" x2="350" y2="105" stroke="#f1f5f9" strokeWidth="1" strokeDasharray="4 4" />
                             <line x1="50" y1="180" x2="350" y2="180" stroke="#cbd5e1" strokeWidth="1" />

                             <rect x="75" y={180 - getBarHeight(avgNn)} width="40" height={getBarHeight(avgNn)} fill="#64748b" rx="4" />
                             <rect x="180" y={180 - getBarHeight(avgGa)} width="40" height={getBarHeight(avgGa)} fill="#2563eb" rx="4" />
                             <rect x="285" y={180 - getBarHeight(avgOrt)} width="40" height={getBarHeight(avgOrt)} fill="#d97706" rx="4" />

                             <text x="95" y={170 - getBarHeight(avgNn)} textAnchor="middle" className="text-[9px] fill-gray-500 font-bold">{(avgNn).toFixed(0)} km</text>
                             <text x="200" y={170 - getBarHeight(avgGa)} textAnchor="middle" className="text-[9px] fill-blue-600 font-bold">{(avgGa).toFixed(0)} km</text>
                             <text x="305" y={170 - getBarHeight(avgOrt)} textAnchor="middle" className="text-[9px] fill-amber-600 font-bold">{(avgOrt).toFixed(0)} km</text>

                             <text x="95" y="195" textAnchor="middle" className="text-[10px] fill-gray-500 font-semibold">Baseline</text>
                             <text x="200" y="195" textAnchor="middle" className="text-[10px] fill-gray-500 font-semibold">Proposed GA</text>
                             <text x="305" y="195" textAnchor="middle" className="text-[10px] fill-gray-500 font-semibold">OR-Tools</text>

                             <text x="45" y="30" textAnchor="end" className="text-[9px] fill-gray-400 font-semibold">Max</text>
                             <text x="45" y="180" textAnchor="end" className="text-[9px] fill-gray-400 font-semibold">0</text>
                           </svg>
                         </div>
                         <div className="flex justify-center gap-6 mt-4">
                           <div className="flex items-center gap-2">
                             <span className="w-3 h-3 rounded-full bg-blue-600" />
                             <span className="text-[10px] text-gray-600 font-semibold">Proposed C++ (GA)</span>
                           </div>
                           <div className="flex items-center gap-2">
                             <span className="w-3 h-3 rounded-full bg-amber-600" />
                             <span className="text-[10px] text-gray-600 font-semibold">Google OR-Tools</span>
                           </div>
                         </div>
                       </div>
                     </div>

                     <div className="bg-white border border-gray-200 rounded-2xl p-6 shadow-sm space-y-6">
                       <div className="flex justify-between items-center border-b border-gray-100 pb-4">
                         <h3 className="text-sm font-bold text-gray-900">Benchmarking Detail Table</h3>
                         <div className="flex gap-2">
                           {categories.map(cat => (
                             <button
                               key={cat}
                               onClick={() => { setAnalyticsCategoryFilter(cat); setMasterTablePage(0); }}
                               className={`px-3 py-1.5 rounded-lg text-xs font-semibold cursor-pointer transition-all border
                                 ${analyticsCategoryFilter === cat 
                                   ? 'bg-blue-50 border-blue-200 text-blue-700' 
                                   : 'bg-white border-gray-300 text-gray-600 hover:bg-gray-50'}`}
                             >
                               {cat}
                             </button>
                           ))}
                         </div>
                       </div>

                       <div className="overflow-x-auto">
                         <table className="w-full text-xs text-center border-collapse">
                           <thead className="bg-[#0f172a] text-white">
                             <tr>
                               <th className="px-4 py-3 font-medium rounded-tl-lg text-left">Instance File</th>
                               <th className="px-4 py-3 font-medium text-left">Category</th>
                               <th className="px-4 py-3 font-medium">Baseline Distance (NN)</th>
                               <th className="px-4 py-3 font-medium">Proposed Distance (GA)</th>
                               <th className="px-4 py-3 font-medium">OR-Tools Distance</th>
                               <th className="px-4 py-3 font-medium">Proposed Time</th>
                               <th className="px-4 py-3 font-medium rounded-tr-lg">ORT Time</th>
                             </tr>
                           </thead>
                           <tbody className="divide-y divide-gray-100">
                             {displayRows.map((row, index) => {
                               const cat = getCategory(row.Instance);

                               const renderDistanceWithFeasibility = (cost: number, undelivered: number, isProposed = false) => {
                                  if (undelivered > 0) {
                                    return <span className="text-red-500 font-semibold">N/A ({undelivered} undelivered)</span>;
                                  }
                                  const distKm = getRawDist(cost, undelivered);
                                 const formattedDist = distKm > 0 ? distKm.toFixed(1) + " km" : "0.0 km";
                                 return (
                                   <div className="flex flex-col items-center justify-center">
                                     <span className={`font-semibold ${isProposed ? 'text-blue-600 font-bold' : 'text-gray-800'}`}>
                                       {formattedDist}
                                     </span>
                                     {undelivered > 0 ? (
                                       <span className="text-[9px] font-bold text-red-600 bg-red-50 px-1.5 py-0.5 rounded-full mt-0.5">
                                         -{undelivered} Undelivered
                                       </span>
                                     ) : (
                                       <span className="text-[9px] font-bold text-green-600 bg-green-50 px-1.5 py-0.5 rounded-full mt-0.5">
                                         100% Feasible
                                       </span>
                                     )}
                                   </div>
                                 );
                               };

                               return (
                                 <tr key={index} className="hover:bg-gray-50/50">
                                   <td className="px-4 py-3.5 text-left font-semibold text-gray-800">{row.Instance}</td>
                                   <td className="px-4 py-3.5 text-left text-gray-500 font-semibold">{cat}</td>
                                   <td className="px-4 py-3.5">{renderDistanceWithFeasibility(row.NN_Cost, row.NN_Undelivered)}</td>
                                   <td className="px-4 py-3.5">{renderDistanceWithFeasibility(row.GA_Cost, row.GA_Undelivered, true)}</td>
                                   <td className="px-4 py-3.5">{renderDistanceWithFeasibility(row.ORT_Cost, row.ORT_Undelivered)}</td>
                                   <td className="px-4 py-3.5">{(row.GA_ms).toFixed(1)} ms</td>
                                   <td className="px-4 py-3.5">{(row.ORT_ms).toFixed(1)} ms</td>
                                 </tr>
                               );
                             })}
                           </tbody>
                         </table>
                       </div>

                       {filtered.length > 10 && (
                         <div className="flex justify-between items-center pt-4 border-t border-gray-100">
                           <span className="text-xs text-gray-500 font-medium">
                             Showing {masterTablePage * 10 + 1} to {Math.min((masterTablePage + 1) * 10, filtered.length)} of {filtered.length} instances
                           </span>
                           <div className="flex gap-2">
                             <button
                               disabled={masterTablePage === 0}
                               onClick={() => setMasterTablePage(prev => prev - 1)}
                               className="px-3 py-1.5 rounded-lg border border-gray-300 text-xs font-semibold bg-white text-gray-700 hover:bg-gray-50 disabled:bg-gray-100 disabled:text-gray-400 cursor-pointer"
                             >
                               Previous
                             </button>
                             <button
                               disabled={masterTablePage === pageCount - 1}
                               onClick={() => setMasterTablePage(prev => prev + 1)}
                               className="px-3 py-1.5 rounded-lg border border-gray-300 text-xs font-semibold bg-white text-gray-700 hover:bg-gray-50 disabled:bg-gray-100 disabled:text-gray-400 cursor-pointer"
                             >
                               Next
                             </button>
                           </div>
                         </div>
                       )}
                     </div>
                   </>
                 );
               })()}
             </div>
          )}
        </div>
      </div>

      {batchRunning && !isProgressMinimized && (
        <div className="fixed inset-0 bg-[#0f172a]/70 backdrop-blur-md flex flex-col items-center justify-center z-50 transition-all duration-300">
          <div className="bg-white rounded-3xl p-8 max-w-md w-full mx-4 shadow-2xl flex flex-col items-center border border-gray-100 animate-in fade-in zoom-in-95 duration-200">
            <div className="relative w-32 h-32 mb-6">
              <svg className="w-full h-full transform -rotate-90">
                <circle
                  cx="64"
                  cy="64"
                  r="54"
                  className="stroke-gray-100"
                  strokeWidth="10"
                  fill="transparent"
                />
                <circle
                  cx="64"
                  cy="64"
                  r="54"
                  className="stroke-blue-600 transition-all duration-300 ease-out"
                  strokeWidth="10"
                  fill="transparent"
                  strokeDasharray={2 * Math.PI * 54}
                  strokeDashoffset={2 * Math.PI * 54 * (1 - (batchProgress.current || 0) / (batchProgress.total || 300))}
                  strokeLinecap="round"
                />
              </svg>
              <div className="absolute inset-0 flex flex-col items-center justify-center">
                <span className="text-2xl font-black text-gray-900">
                  {Math.round(((batchProgress.current || 0) / (batchProgress.total || 300)) * 100)}%
                </span>
                <span className="text-[10px] text-gray-400 font-bold uppercase tracking-wider mt-0.5">
                  Progress
                </span>
              </div>
            </div>

            <h3 className="text-lg font-bold text-gray-900 mb-2 text-center">
              Running Fleet Benchmarks
            </h3>
            
            <p className="text-xs text-gray-500 text-center leading-relaxed mb-6 px-4">
              Evaluating routing convergence, zone clusters, and OSRM matrix calls across the fleet...
            </p>

            <div className="w-full bg-gray-50 rounded-2xl p-4 border border-gray-100 flex flex-col items-center">
              <span className="text-[10px] text-gray-400 font-bold uppercase tracking-wider mb-1">
                Active Instance
              </span>
              <span className="text-xs font-semibold text-gray-700 text-center truncate w-full px-2">
                {batchProgress.activeInstance || "Loading..."}
              </span>
              <span className="text-[10px] text-blue-600 font-bold mt-2 bg-blue-50 px-2 py-0.5 rounded-full">
                {batchProgress.current || 0} / {batchProgress.total || 300} Completed
              </span>
            </div>

            <div className="flex gap-4 w-full mt-6">
              <button
                onClick={() => setIsProgressMinimized(true)}
                className="flex-1 bg-gray-100 hover:bg-gray-200 text-gray-700 font-semibold py-2.5 rounded-xl text-xs transition-colors cursor-pointer"
              >
                Run in Background
              </button>
              <button
                onClick={stopBatchSimulation}
                className="flex-1 bg-red-50 hover:bg-red-100 text-red-600 font-semibold py-2.5 rounded-xl text-xs transition-colors cursor-pointer"
              >
                Interrupt Run
              </button>
            </div>
          </div>
        </div>
      )}

      {batchRunning && isProgressMinimized && (
        <div className="fixed bottom-6 right-6 w-80 bg-white border border-gray-200 rounded-2xl p-4 shadow-2xl z-50 flex items-center gap-4 animate-in slide-in-from-bottom-5 duration-200">
          <div className="relative w-12 h-12 shrink-0">
            <svg className="w-full h-full transform -rotate-90">
              <circle cx="24" cy="24" r="20" className="stroke-gray-100" strokeWidth="4" fill="transparent" />
              <circle cx="24" cy="24" r="20" className="stroke-blue-600 transition-all duration-300" strokeWidth="4" fill="transparent" strokeDasharray={2 * Math.PI * 20} strokeDashoffset={2 * Math.PI * 20 * (1 - (batchProgress.current || 0) / (batchProgress.total || 300))} strokeLinecap="round" />
            </svg>
            <div className="absolute inset-0 flex items-center justify-center">
              <span className="text-[10px] font-black text-gray-900">
                {Math.round(((batchProgress.current || 0) / (batchProgress.total || 300)) * 100)}%
              </span>
            </div>
          </div>
          
          <div className="flex-1 min-w-0">
            <p className="text-[10px] text-gray-400 font-bold uppercase tracking-wider">Fleet Benchmarking</p>
            <p className="text-xs font-semibold text-gray-700 truncate mt-0.5">{batchProgress.activeInstance || "Processing..."}</p>
            <span className="text-[9px] text-blue-600 bg-blue-50 font-bold px-1.5 py-0.5 rounded-full mt-1 inline-block">
              {batchProgress.current || 0} / {batchProgress.total || 300} Complete
            </span>
          </div>

          <div className="flex flex-col gap-1.5 shrink-0">
            <button
              onClick={() => setIsProgressMinimized(false)}
              className="p-1.5 rounded-lg bg-gray-100 hover:bg-gray-200 text-gray-600 transition-colors cursor-pointer"
              title="Maximize Progress"
            >
              <svg className="w-3.5 h-3.5" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2.5} d="M4 8V4h4m12 4V4h-4M4 16v4h4m12-4v4h-4" />
              </svg>
            </button>
            <button
              onClick={stopBatchSimulation}
              className="p-1.5 rounded-lg bg-red-50 hover:bg-red-100 text-red-600 transition-colors cursor-pointer"
              title="Interrupt Run"
            >
              <svg className="w-3.5 h-3.5" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2.5} d="M6 18L18 6M6 6l12 12" />
              </svg>
            </button>
          </div>
        </div>
      )}
    </div>
  );
}
