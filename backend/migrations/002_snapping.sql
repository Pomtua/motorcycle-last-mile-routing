ALTER TABLE datasets
  ADD COLUMN depot_input       geometry(Point, 4326),
  ADD COLUMN depot_snap_m      double precision CHECK (depot_snap_m >= 0),
  ADD COLUMN max_snap_m        double precision CHECK (max_snap_m > 0);

ALTER TABLE customers
  ADD COLUMN input_location    geometry(Point, 4326),
  ADD COLUMN snap_m            double precision CHECK (snap_m >= 0);
