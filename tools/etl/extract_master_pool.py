import psycopg2
import json
import os
import csv
from dotenv import load_dotenv

load_dotenv()

def extract_master_pool(limit=10000):
    print(f"--- Starting Extraction of {limit} points ---")
    
    try:
        conn = psycopg2.connect(
            host="localhost",
            port=os.getenv("POSTGRES_PORT", 5432),
            dbname=os.getenv("POSTGRES_DB"),
            user=os.getenv("POSTGRES_USER"),
            password=os.getenv("POSTGRES_PASSWORD")
        )
        cur = conn.cursor()

        # Bangkok and its surrounding provinces
        query = """
        SELECT 
            osm_id, 
            type, 
            ST_X(location) as lng, 
            ST_Y(location) as lat 
        FROM delivery_pool 
         WHERE type IN %s
             AND ST_X(location) BETWEEN 100.2 AND 100.9 
             AND ST_Y(location) BETWEEN 13.4 AND 14.1
        ORDER BY RANDOM() 
        LIMIT %s;
        """

        types_to_filter = ('house', 'apartments', 'residential', 'school', 'office', 'retail', 'university', 'hospital')

        cur.execute(query, (types_to_filter, limit))
        rows = cur.fetchall()

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
    finally:
        if 'conn' in locals():
            cur.close()
            conn.close()

if __name__ == "__main__":
    extract_master_pool(10000)