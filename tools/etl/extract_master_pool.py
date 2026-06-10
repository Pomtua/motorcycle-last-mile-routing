import psycopg
import json
import os
import csv
from dotenv import load_dotenv

load_dotenv()

def extract_master_pool(limit=10000):
    print(f"--- Starting Extraction of {limit} points ---")

    conn_str = f"host=localhost port={os.getenv('POSTGRES_PORT', 5432)} dbname={os.getenv('POSTGRES_DB')} user={os.getenv('POSTGRES_USER')} password={os.getenv('POSTGRES_PASSWORD')}"
    
    try:
        with psycopg.connect(conn_str) as conn:
            # Bangkok and its surrounding provinces
            query = """
            SELECT
                osm_id,
                type,
                ST_X(location) as lng,
                ST_Y(location) as lat
            FROM delivery_pool
            WHERE type = ANY(%s)
            AND location && ST_MakeEnvelope(100.2, 13.4, 100.9, 14.1, 4326)
            ORDER BY RANDOM()
            LIMIT %s;
            """

            types_to_filter = ['house', 'apartments', 'residential', 'school', 'office', 'retail', 'university', 'hospital']

            rows = conn.execute(query, (types_to_filter, limit)).fetchall()

            os.makedirs('data/master_pool', exist_ok=True)
            output_file = 'data/master_pool/raw_coordinates.csv'
            
            with open(output_file, 'w', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                writer.writerow(['osm_id', 'type', 'lng', 'lat']) 
                writer.writerows(rows)

            print(f"Extracted {len(rows)} points.")
            print(f"File saved at: {output_file}")

    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    extract_master_pool(10000)